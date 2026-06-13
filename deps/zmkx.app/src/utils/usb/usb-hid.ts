import type {
  IUsbCommTransport,
  OnDisconnected,
  OnList,
  OnMessage,
  OnTransportTrace,
  UsbTransportTrace,
} from './usb';
import { UsbComm } from '@/proto/comm.proto';

const USB_VID = 0x1d50;
const USB_PID = 0x615e;

const USB_COMM_USAGE_PAGE = 0xff14;
const HID_COMM_REPORT_COUNT = 63;
const USB_COMM_PAYLOAD_SIZE = HID_COMM_REPORT_COUNT - 1;
const USB_COMM_MAX_RX_QUEUE = 4096;

export class UsbCommHidTransport implements IUsbCommTransport<HIDDevice> {

  private device: HIDDevice | undefined;

  private reportOut: number | undefined;

  private queueIn: Uint8Array | undefined;

  private sendQueue = Promise.resolve();

  constructor(
    private readonly onList: OnList,
    private readonly onMessage: OnMessage,
    private readonly onDisconnected: OnDisconnected,
    private readonly onTransportTrace: OnTransportTrace,
  ) {
    navigator.hid?.addEventListener('connect', () => {
      this.refreshList();
    });

    navigator.hid?.addEventListener('disconnect', ({ device }) => {
      this.refreshList();
      if (this.device == device) {
        device.removeEventListener('inputreport', this.handleInputEvent);
        this.resetSessionState();
        this.onDisconnected();
        this.device = undefined;
      }
    });

    this.refreshList();

    this.handleInputEvent = this.handleInputEvent.bind(this);
  }

  private async refreshList(): Promise<void> {
    const devices = await navigator.hid?.getDevices();
    this.onList(devices.filter(filterDevice));
  }

  private resetSessionState(): void {
    this.reportOut = undefined;
    this.queueIn = undefined;
    this.sendQueue = Promise.resolve();
  }

  async pick(device: HIDDevice): Promise<HIDDevice | undefined> {
    if (!filterDevice(device)) {
      return undefined;
    }

    if (this.device && this.device !== device) {
      this.device.removeEventListener('inputreport', this.handleInputEvent);
    }

    this.resetSessionState();
    await device.open();

    this.device = device;
    this.reportOut = device.collections![0].outputReports![0].reportId;

    this.device.addEventListener('inputreport', this.handleInputEvent);

    return this.device;
  }

  async request(): Promise<HIDDevice | undefined> {
    const devices = await navigator.hid?.requestDevice({
      filters: [
        { vendorId: USB_VID, productId: USB_PID },
      ],
    });

    this.refreshList();

    const device = devices.filter(filterDevice)[0];
    if (!device) {
      return undefined;
    }

    return await this.pick(device);
  }

  async close(): Promise<void> {
    const device = this.device;
    this.device = undefined;
    this.resetSessionState();
    if (device) {
      this.onDisconnected();
      device.removeEventListener('inputreport', this.handleInputEvent);
      await device.close();
    }
  }

  async send(req: UsbComm.IMessageH2D): Promise<void> {
    const sendTask = async (): Promise<void> => {
      if (!this.device || this.reportOut === undefined) {
        return;
      }

      if (this.queueIn?.length) {
        this.emitTrace({
          kind: 'rx-drop',
          summary: `Dropped stale RX buffer before sending ${UsbComm.Action[req.action] ?? `Action ${req.action}`}`,
          action: UsbComm.Action[req.action] ?? `Action ${req.action}`,
          queueLength: this.queueIn.length,
          rawHex: toHex(this.queueIn),
        });
        this.queueIn = undefined;
      }

      const message = UsbComm.MessageH2D.encodeDelimited(req).finish();
      this.emitTrace({
        kind: 'tx-message',
        summary: `TX ${UsbComm.Action[req.action] ?? `Action ${req.action}`} message (${message.length} bytes)`,
        action: UsbComm.Action[req.action] ?? `Action ${req.action}`,
        messageLength: message.length,
        rawHex: toHex(message),
      });

      // NOTE: We do want a 0-byte buffer as EOF
      for (let i = 0; i <= message.length; i += USB_COMM_PAYLOAD_SIZE) {
        const buf = message.subarray(i, i + USB_COMM_PAYLOAD_SIZE);
        const out = new Uint8Array(HID_COMM_REPORT_COUNT);
        out[0] = buf.length;
        out.set(buf, 1);
        this.emitTrace({
          kind: 'tx-packet',
          summary: `TX packet ${buf.length} bytes`,
          action: UsbComm.Action[req.action] ?? `Action ${req.action}`,
          packetLength: buf.length,
          rawHex: toHex(out.subarray(0, Math.min(out.length, buf.length + 1))),
        });
        await this.device.sendReport(this.reportOut, out);
      }
    };

    const queuedTask = this.sendQueue.then(sendTask, sendTask);
    this.sendQueue = queuedTask.catch(() => {});
    await queuedTask;
  }

  private handleInputEvent(ev: HIDInputReportEvent): void {
    this.handleInput(new Uint8Array(ev.data.buffer, ev.data.byteOffset, ev.data.byteLength));
  }

  private handleInput(data: Uint8Array): void {
    const len = data[0];
    this.queueIn = concat(this.queueIn, data.subarray(1, 1 + len));
    this.emitTrace({
      kind: 'rx-packet',
      summary: `RX packet ${len} bytes`,
      packetLength: len,
      queueLength: this.queueIn.length,
      rawHex: toHex(data.subarray(0, Math.min(data.length, len + 1))),
    });

    if (this.queueIn.length > USB_COMM_MAX_RX_QUEUE) {
      this.emitTrace({
        kind: 'rx-overflow',
        summary: `RX queue overflow (${this.queueIn.length} bytes)`,
        queueLength: this.queueIn.length,
        rawHex: toHex(this.queueIn),
      });
      this.queueIn = undefined;
      return;
    }

    try {
      const res = UsbComm.MessageD2H.decodeDelimited(this.queueIn);
      const actionName = UsbComm.Action[res.action];
      if (actionName === undefined) {
        console.warn('[usb-hid] Dropping malformed response with invalid action', {
          action: res.action,
          queueLength: this.queueIn.length,
        });
        this.emitTrace({
          kind: 'rx-drop',
          summary: `Dropped malformed response with invalid action ${res.action}`,
          queueLength: this.queueIn.length,
          rawHex: toHex(this.queueIn),
        });
        this.queueIn = undefined;
        return;
      }

      this.emitTrace({
        kind: 'rx-decode',
        summary: `Decoded ${actionName} (${res.payload ?? 'no payload'})`,
        action: actionName,
        payload: res.payload,
        messageLength: this.queueIn.length,
        rawHex: toHex(this.queueIn),
      });
      this.queueIn = undefined;
      this.onMessage(res);
    } catch (error) {
      const frame = getDelimitedFrame(this.queueIn);
      if (!frame) {
        return;
      }

      const recovered = tryRecoverLogEvents(this.queueIn);
      if (recovered) {
        if (recovered.skippedEvents > 0) {
          this.emitTrace({
            kind: 'rx-drop',
            summary: `Recovered LOG_GET_EVENTS after skipping ${recovered.skippedEvents} malformed event(s)`,
            action: 'LOG_GET_EVENTS',
            payload: 'logEvents',
            messageLength: this.queueIn.length,
            rawHex: toHex(this.queueIn),
          });
        }

        this.emitTrace({
          kind: 'rx-decode',
          summary: `Recovered LOG_GET_EVENTS (${recovered.message.logEvents?.events?.length ?? 0} events)`,
          action: 'LOG_GET_EVENTS',
          payload: 'logEvents',
          messageLength: this.queueIn.length,
          rawHex: toHex(this.queueIn),
        });
        this.queueIn = undefined;
        this.onMessage(recovered.message);
        return;
      }

      this.emitTrace({
        kind: 'rx-drop',
        summary: `Dropped malformed RX message: ${formatDecodeError(error)}`,
        queueLength: this.queueIn.length,
        rawHex: toHex(this.queueIn),
      });
      this.queueIn = undefined;
    }
  }

  private emitTrace(trace: Omit<UsbTransportTrace, 'ts'>): void {
    this.onTransportTrace({
      ts: Date.now(),
      ...trace,
    });
  }

}

type DelimitedFrame = {
  headerLength: number;
  messageLength: number;
  totalLength: number;
};

type RecoveredLogEvents = {
  message: UsbComm.MessageD2H;
  skippedEvents: number;
};

function tryRecoverLogEvents(bytes: Uint8Array): RecoveredLogEvents | undefined {
  const frame = getDelimitedFrame(bytes);
  if (!frame || frame.totalLength !== bytes.length) {
    return undefined;
  }

  let action: number | undefined;
  let payloadBytes: Uint8Array | undefined;
  let offset = frame.headerLength;
  const end = frame.totalLength;

  while (offset < end) {
    const key = readVarint(bytes, offset);
    if (!key) {
      return undefined;
    }
    offset = key.nextOffset;
    const fieldNumber = key.value >>> 3;
    const wireType = key.value & 0x07;

    if (fieldNumber === 1 && wireType === 0) {
      const value = readVarint(bytes, offset);
      if (!value) {
        return undefined;
      }
      action = value.value;
      offset = value.nextOffset;
      continue;
    }

    if (fieldNumber === 11 && wireType === 2) {
      const value = readLengthDelimited(bytes, offset);
      if (!value) {
        return undefined;
      }
      payloadBytes = value.bytes;
      offset = value.nextOffset;
      continue;
    }

    const skippedOffset = skipWireValue(bytes, offset, wireType);
    if (skippedOffset === undefined) {
      return undefined;
    }
    offset = skippedOffset;
  }

  if (action !== UsbComm.Action.LOG_GET_EVENTS || !payloadBytes) {
    return undefined;
  }

  const events: UsbComm.LogEvent[] = [];
  let droppedCount: number | undefined;
  let oldestSeq: number | undefined;
  let newestSeq: number | undefined;
  let skippedEvents = 0;
  offset = 0;

  while (offset < payloadBytes.length) {
    const key = readVarint(payloadBytes, offset);
    if (!key) {
      return undefined;
    }
    offset = key.nextOffset;
    const fieldNumber = key.value >>> 3;
    const wireType = key.value & 0x07;

    if (fieldNumber === 1 && wireType === 2) {
      const value = readLengthDelimited(payloadBytes, offset);
      if (!value) {
        return undefined;
      }

      try {
        events.push(UsbComm.LogEvent.decode(value.bytes));
      } catch {
        skippedEvents++;
      }

      offset = value.nextOffset;
      continue;
    }

    if ((fieldNumber === 2 || fieldNumber === 3 || fieldNumber === 4) && wireType === 0) {
      const value = readVarint(payloadBytes, offset);
      if (!value) {
        return undefined;
      }
      offset = value.nextOffset;
      if (fieldNumber === 2) {
        droppedCount = value.value;
      } else if (fieldNumber === 3) {
        oldestSeq = value.value;
      } else {
        newestSeq = value.value;
      }
      continue;
    }

    const skippedOffset = skipWireValue(payloadBytes, offset, wireType);
    if (skippedOffset === undefined) {
      return undefined;
    }
    offset = skippedOffset;
  }

  if (events.length === 0 && skippedEvents === 0) {
    return undefined;
  }

  return {
    message: UsbComm.MessageD2H.create({
      action: UsbComm.Action.LOG_GET_EVENTS,
      logEvents: UsbComm.LogEvents.create({
        events,
        droppedCount,
        oldestSeq,
        newestSeq,
      }),
    }),
    skippedEvents,
  };
}

function getDelimitedFrame(bytes: Uint8Array): DelimitedFrame | undefined {
  const length = readVarint(bytes, 0);
  if (!length) {
    return undefined;
  }

  const totalLength = length.nextOffset + length.value;
  if (bytes.length < totalLength) {
    return undefined;
  }

  return {
    headerLength: length.nextOffset,
    messageLength: length.value,
    totalLength,
  };
}

type VarintRead = {
  value: number;
  nextOffset: number;
};

type LengthDelimitedRead = {
  bytes: Uint8Array;
  nextOffset: number;
};

function readVarint(bytes: Uint8Array, startOffset: number): VarintRead | undefined {
  let value = 0;
  let shift = 0;
  let offset = startOffset;

  while (offset < bytes.length && shift < 35) {
    const byte = bytes[offset++];
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) {
      return { value, nextOffset: offset };
    }
    shift += 7;
  }

  return undefined;
}

function readLengthDelimited(bytes: Uint8Array, offset: number): LengthDelimitedRead | undefined {
  const length = readVarint(bytes, offset);
  if (!length) {
    return undefined;
  }

  const endOffset = length.nextOffset + length.value;
  if (endOffset > bytes.length) {
    return undefined;
  }

  return {
    bytes: bytes.subarray(length.nextOffset, endOffset),
    nextOffset: endOffset,
  };
}

function skipWireValue(bytes: Uint8Array, offset: number, wireType: number): number | undefined {
  if (wireType === 0) {
    return readVarint(bytes, offset)?.nextOffset;
  }
  if (wireType === 1) {
    return offset + 8 <= bytes.length ? offset + 8 : undefined;
  }
  if (wireType === 2) {
    return readLengthDelimited(bytes, offset)?.nextOffset;
  }
  if (wireType === 5) {
    return offset + 4 <= bytes.length ? offset + 4 : undefined;
  }
  return undefined;
}

function formatDecodeError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function filterDevice(device: HIDDevice): boolean {
  return device.vendorId == USB_VID &&
    device.productId == USB_PID &&
    device.collections.some(({ usagePage, inputReports, outputReports }) =>
      usagePage == USB_COMM_USAGE_PAGE &&
      inputReports?.length == 1 &&
      outputReports?.length == 1
    );
}

function concat(head: Uint8Array | undefined, tail: Uint8Array): Uint8Array {
  return head ? new Uint8Array([...head, ...tail]) : tail;
}

function toHex(data: Uint8Array): string {
  return Array.from(data)
    .map((value) => value.toString(16).padStart(2, '0'))
    .join(' ');
}
