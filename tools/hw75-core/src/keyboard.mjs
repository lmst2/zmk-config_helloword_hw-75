import { EventEmitter } from 'node:events';
import HID from 'node-hid';

import { UsbComm } from './protoLoader.mjs';

const USB_VID = 0x1d50;
const USB_PID = 0x615e;
const USB_COMM_USAGE_PAGE = 0xff14;
const HID_REPORT_ID = 1;
const HID_REPORT_SIZE = 63;
const PAYLOAD_PER_PACKET = HID_REPORT_SIZE - 1;
const RX_QUEUE_LIMIT = 8192;
const RESPONSE_TIMEOUT_MS = 2000;
const RESCAN_INTERVAL_MS = 1000;

/*
 * hw75_keyboard and hw75_dynamic share VID/PID and both expose the usb_comm
 * usage page. We MUST only attach to the dynamic: its firmware is the one
 * that implements the new EINK / KNOB_CALIBRATION actions, and connecting to
 * the keyboard instead leaves the helper shouting into a void
 * ("Action 26 response timeout" spam + decode failures because the HID reads
 * back the keyboard's main HID input reports).
 *
 * Windows node-hid always reports the USB Product string, so we match on it.
 * If the product string is missing (non-Windows edge case) we fall back to
 * serial number hints; worst case we refuse to connect and log why.
 */
const TARGET_PRODUCT_HINT = 'dynamic';

function matchesUsage(info, productHint) {
  if (info.vendorId !== USB_VID || info.productId !== USB_PID) {
    return false;
  }
  if (info.usagePage !== USB_COMM_USAGE_PAGE) {
    return false;
  }
  const product = (info.product || '').toLowerCase();
  if (!product) {
    return false;
  }
  return product.includes(productHint);
}

let enumerationLogged = false;

function logEnumerationOnce(devices) {
  if (enumerationLogged) {
    return;
  }
  const hw75 = devices.filter((d) => d.vendorId === USB_VID && d.productId === USB_PID);
  if (hw75.length === 0) {
    return;
  }
  enumerationLogged = true;
  console.log(`[hid] hw75 HID interfaces found: ${hw75.length}`);
  for (const d of hw75) {
    console.log(
      `  - product=${JSON.stringify(d.product)} usagePage=0x${(d.usagePage ?? 0).toString(16)} ` +
        `interface=${d.interface} path=${d.path}`);
  }
}

function findDevicePath(productHint) {
  const devices = HID.devices();
  logEnumerationOnce(devices);
  const target = devices.find((info) => matchesUsage(info, productHint));
  return target?.path;
}

function prefixSize(buf) {
  /* Reads the protobuf varint length prefix; returns { headerLen, totalLen }. */
  let length = 0;
  let shift = 0;
  for (let i = 0; i < buf.length; i++) {
    const byte = buf[i];
    length |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) {
      return { headerLen: i + 1, totalLen: i + 1 + length };
    }
    shift += 7;
    if (shift >= 35) {
      return undefined;
    }
  }
  return undefined;
}

export class Device extends EventEmitter {
  /*
   * Both HW-75 boards share VID/PID and expose the usb_comm usage page, so we
   * select by USB product string: productHint 'dynamic' -> the knob/e-ink module,
   * 'keyboard' -> the 82-key board (touchbar / function slots / RGB). The 中枢
   * runs one Device per board so every connection goes through it.
   */
  constructor({ productHint = TARGET_PRODUCT_HINT, name = 'dynamic' } = {}) {
    super();
    this.productHint = productHint;
    this.name = name;
    this.device = undefined;
    this.devicePath = undefined;
    this.rxBuf = Buffer.alloc(0);
    this.pending = undefined;
    this.txQueue = Promise.resolve();
    this.rescanTimer = undefined;
  }

  start() {
    this.scheduleRescan(0);
  }

  stop() {
    if (this.rescanTimer) {
      clearTimeout(this.rescanTimer);
      this.rescanTimer = undefined;
    }
    this.closeDevice();
  }

  isConnected() {
    return !!this.device;
  }

  scheduleRescan(delay = RESCAN_INTERVAL_MS) {
    if (this.rescanTimer) {
      clearTimeout(this.rescanTimer);
    }
    this.rescanTimer = setTimeout(() => this.tryOpenDevice(), delay);
  }

  tryOpenDevice() {
    if (this.device) {
      return;
    }

    const path = findDevicePath(this.productHint);
    if (!path) {
      this.scheduleRescan();
      return;
    }

    try {
      const device = new HID.HID(path);
      device.on('data', (data) => this.handleRx(data));
      device.on('error', (err) => this.handleDeviceError(err));

      this.device = device;
      this.devicePath = path;
      this.rxBuf = Buffer.alloc(0);
      this.emit('status', { connected: true, path });
      console.log(`[${this.name}] connected: ${path}`);
    } catch (error) {
      console.warn(`[${this.name}] failed to open ${path}: ${error.message}`);
      this.scheduleRescan();
    }
  }

  closeDevice() {
    const device = this.device;
    this.device = undefined;
    this.devicePath = undefined;
    this.rxBuf = Buffer.alloc(0);

    const pending = this.pending;
    this.pending = undefined;
    if (pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error('Device disconnected'));
    }

    if (device) {
      try {
        device.close();
      } catch {
        /* ignore */
      }
      this.emit('status', { connected: false });
    }
  }

  handleDeviceError(err) {
    console.warn(`[${this.name}] HID error: ${err.message}`);
    this.closeDevice();
    this.scheduleRescan();
  }

  handleRx(data) {
    if (!data || data.length === 0) {
      return;
    }

    /*
     * node-hid on Windows keeps the HID report id as the first byte of every
     * input report when the device uses numbered reports (ZMK's usb_comm
     * interface does: report_id=1). WebHID strips the id transparently, but
     * node-hid does not, so we skip it manually. Fall back to offset=0 for the
     * edge case where the platform already strips it (report size matches).
     */
    const reportIdOffset = data.length > HID_REPORT_SIZE ? 1 : 0;
    const len = data[reportIdOffset];
    if (len < 0 || len > PAYLOAD_PER_PACKET) {
      console.warn(`[${this.name}] invalid packet length: ${len}`);
      this.rxBuf = Buffer.alloc(0);
      return;
    }

    const payload = data.slice(reportIdOffset + 1, reportIdOffset + 1 + len);
    this.rxBuf = Buffer.concat([this.rxBuf, payload]);

    if (this.rxBuf.length > RX_QUEUE_LIMIT) {
      console.warn(`[${this.name}] rx queue overflow (${this.rxBuf.length}), dropping`);
      this.rxBuf = Buffer.alloc(0);
      return;
    }

    const frame = prefixSize(this.rxBuf);
    if (!frame || this.rxBuf.length < frame.totalLen) {
      return;
    }

    const messageBytes = this.rxBuf.slice(0, frame.totalLen);
    this.rxBuf = this.rxBuf.slice(frame.totalLen);

    let message;
    try {
      message = UsbComm.MessageD2H.decodeDelimited(messageBytes);
    } catch (error) {
      console.warn(`[${this.name}] decode failed: ${error.message}`);
      return;
    }

    if (this.pending && this.pending.action === message.action) {
      const pending = this.pending;
      this.pending = undefined;
      clearTimeout(pending.timer);
      pending.resolve(message);
    } else {
      this.emit('message', message);
    }
  }

  async send(h2d) {
    const task = () => this.sendImpl(h2d);
    const chained = this.txQueue.then(task, task);
    this.txQueue = chained.catch(() => {});
    return chained;
  }

  async sendImpl(h2d) {
    if (!this.device) {
      throw new Error('Keyboard not connected');
    }
    if (this.pending) {
      throw new Error(`Keyboard busy awaiting action ${this.pending.action}`);
    }

    const action = h2d.action;
    const encoded = UsbComm.MessageH2D.encodeDelimited(h2d).finish();

    const responsePromise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pending?.action === action) {
          this.pending = undefined;
          reject(new Error(`Action ${action} response timeout`));
        }
      }, RESPONSE_TIMEOUT_MS);
      this.pending = { action, resolve, reject, timer };
    });

    try {
      for (let offset = 0; offset <= encoded.length; offset += PAYLOAD_PER_PACKET) {
        const chunk = encoded.subarray(offset, offset + PAYLOAD_PER_PACKET);
        /* node-hid write wants [reportId, ...reportData]; report size is 63 bytes. */
        const packet = Buffer.alloc(HID_REPORT_SIZE + 1);
        packet[0] = HID_REPORT_ID;
        packet[1] = chunk.length;
        packet.set(chunk, 2); /* Uint8Array.set — works whether chunk is Buffer or Uint8Array */
        this.device.write(packet);
      }
    } catch (error) {
      if (this.pending?.action === action) {
        clearTimeout(this.pending.timer);
        this.pending = undefined;
      }
      this.handleDeviceError(error);
      throw error;
    }

    return responsePromise;
  }
}

export { UsbComm };
