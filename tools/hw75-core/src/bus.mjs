/*
 * WebSocket bridge: lets the web UI proxy USB requests through helper-core and
 * subscribe to asynchronous events pushed from the keyboard / weather / clock.
 */
import { WebSocketServer } from 'ws';

import { UsbComm } from './protoLoader.mjs';

/* Binary WS frame types (all little-endian, request id is u32).
 * Wire contract — must match deps/zmkx.app/src/stores/helperCore.ts. */
const FRAME_SEND_REQUEST = 0x01;
const FRAME_SEND_RESPONSE = 0x02;
const FRAME_SEND_ERROR = 0x03;
const FRAME_KEYBOARD_EVENT = 0x04;

const META_SEND_TIMEOUT_MS = 2500;

/* usb_comm actions that belong to the keyboard board; everything else routes to
 * the dynamic module. RGB targets the keyboard's 103-LED underglow; LOG/VERSION
 * are special-cased (LOG -> dynamic by default; VERSION -> both, merged). */
function keyboardActions(UsbComm) {
  const A = UsbComm.Action;
  return new Set([
    A.TOUCHBAR_GET_CONFIG, A.TOUCHBAR_SET_CONFIG,
    A.FUNCTION_SLOT_GET_CONFIG, A.FUNCTION_SLOT_SET_CONFIG,
    A.FUNCTION_SLOT_TRIGGER_EVENT_GET, A.FUNCTION_SLOT_GET_FIRMWARE_CAPS,
    A.RGB_CONTROL, A.RGB_GET_STATE, A.RGB_SET_STATE, A.RGB_GET_INDICATOR, A.RGB_SET_INDICATOR,
  ]);
}

const VERSION_FEATURE_FLAGS = [
  'rgb', 'rgbFullControl', 'rgbIndicator', 'eink', 'knob', 'knobPrefs', 'knobProfileSwitch',
  'knobSpringReport', 'debugLog', 'touchbar', 'touchbarConfig', 'functionSlots', 'einkModes',
  'knobCalibration',
];

export class Bus {
  constructor({ httpServer, keyboard, keyboardBoard, coreConfig, weather }) {
    this.keyboard = keyboard;            // the dynamic module session
    this.keyboardBoard = keyboardBoard;  // the keyboard board session
    this.coreConfig = coreConfig;
    this.weather = weather;
    this.latestWeatherPayload = undefined;
    this.latestClockPayload = undefined;
    this.keyboardActions = keyboardActions(UsbComm);

    this.wss = new WebSocketServer({ server: httpServer, path: '/ws' });
    this.wss.on('connection', (socket) => this.handleConnection(socket));

    keyboard.on('message', (message) => this.broadcastKeyboardEvent(message));
    keyboard.on('status', (status) => this.broadcastStatus(status));
    keyboardBoard?.on('message', (message) => this.broadcastKeyboardEvent(message));

    coreConfig.on('change', (config) => this.broadcastMeta('core-config', { config }));
  }

  deviceFor(action) {
    return this.keyboardActions.has(action) ? this.keyboardBoard : this.keyboard;
  }

  handleConnection(socket) {
    socket.on('message', (data, isBinary) => {
      if (isBinary) {
        this.handleBinaryMessage(socket, data);
      } else {
        this.handleJsonMessage(socket, data.toString('utf8'));
      }
    });

    socket.on('error', (err) => console.warn(`[bus] socket error: ${err.message}`));

    /* Send hello state so client knows about connection + config. */
    this.sendMeta(socket, 'hello', {
      version: 1,
      keyboard: { connected: this.keyboard.isConnected() },
      keyboardBoard: { connected: this.keyboardBoard?.isConnected() ?? false },
      config: this.coreConfig.snapshot(),
      lastWeather: this.latestWeatherPayload,
      lastClock: this.latestClockPayload,
    });
  }

  handleBinaryMessage(socket, buf) {
    if (buf.length < 5) {
      return;
    }

    const frameType = buf[0];
    const requestId = buf.readUInt32LE(1);
    const payload = buf.slice(5);

    if (frameType !== FRAME_SEND_REQUEST) {
      /* Clients only emit send-request; ignore unknown frames. */
      return;
    }

    let h2d;
    try {
      h2d = UsbComm.MessageH2D.decodeDelimited(payload);
    } catch (error) {
      this.replyError(socket, requestId, `Bad H2D: ${error.message}`);
      return;
    }

    this.dispatchSend(socket, requestId, h2d).catch((error) => {
      this.replyError(socket, requestId, error.message);
    });
  }

  async dispatchSend(socket, requestId, h2d) {
    if (h2d.action === UsbComm.Action.VERSION) {
      const merged = await this.queryMergedVersion(h2d);
      this.replyResponse(socket, requestId, merged);
      return;
    }

    const dev = this.deviceFor(h2d.action);
    if (!dev || !dev.isConnected()) {
      const name = dev === this.keyboardBoard ? 'keyboard' : 'dynamic';
      throw new Error(`${name} board not connected`);
    }

    const d2h = await Promise.race([
      dev.send(h2d),
      new Promise((_, reject) =>
        setTimeout(() => reject(new Error('Helper-core forward timeout')), META_SEND_TIMEOUT_MS)),
    ]);
    this.replyResponse(socket, requestId, d2h);
  }

  /* VERSION is special: query both boards and OR their feature flags so the web
   * sees the union (touchbar from the keyboard, knob/e-ink from the dynamic). */
  async queryMergedVersion(h2d) {
    const targets = [this.keyboard, this.keyboardBoard].filter((d) => d?.isConnected());
    if (!targets.length) {
      throw new Error('No board connected');
    }

    const results = await Promise.allSettled(targets.map((d) => d.send(h2d)));
    const versions = results.filter((r) => r.status === 'fulfilled').map((r) => r.value);
    if (!versions.length) {
      throw new Error(results[0]?.reason?.message || 'VERSION failed');
    }

    const base = versions[0];
    const bf = base.version?.features;
    for (let i = 1; i < versions.length && bf; i++) {
      const f = versions[i].version?.features;
      if (!f) {
        continue;
      }
      for (const key of VERSION_FEATURE_FLAGS) {
        if (f[key]) {
          bf[key] = true;
        }
      }
      if (f.rgbEffectCount) {
        bf.rgbEffectCount = Math.max(bf.rgbEffectCount || 0, f.rgbEffectCount);
      }
      if (f.rgbEffectMask) {
        bf.rgbEffectMask = (bf.rgbEffectMask || 0) | f.rgbEffectMask;
      }
    }
    return base;
  }

  handleJsonMessage(socket, text) {
    let msg;
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }

    if (msg?.type === 'get-core-config') {
      this.sendMeta(socket, 'core-config', { config: this.coreConfig.snapshot() });
      return;
    }
    if (msg?.type === 'set-core-config' && msg.config) {
      this.coreConfig.update(msg.config).catch((err) =>
        this.sendMeta(socket, 'error', { error: err.message }));
      return;
    }
    if (msg?.type === 'weather-refresh') {
      this.weather?.refreshNow().catch((err) =>
        this.sendMeta(socket, 'error', { error: err.message }));
      return;
    }
  }

  replyResponse(socket, requestId, d2h) {
    const encoded = UsbComm.MessageD2H.encodeDelimited(d2h).finish();
    const out = Buffer.alloc(5 + encoded.length);
    out[0] = FRAME_SEND_RESPONSE;
    out.writeUInt32LE(requestId, 1);
    Buffer.from(encoded).copy(out, 5);
    this.safeSend(socket, out);
  }

  replyError(socket, requestId, error) {
    const msg = Buffer.from(error, 'utf8');
    const out = Buffer.alloc(5 + msg.length);
    out[0] = FRAME_SEND_ERROR;
    out.writeUInt32LE(requestId, 1);
    msg.copy(out, 5);
    this.safeSend(socket, out);
  }

  broadcastKeyboardEvent(d2h) {
    const encoded = UsbComm.MessageD2H.encodeDelimited(d2h).finish();
    const out = Buffer.alloc(5 + encoded.length);
    out[0] = FRAME_KEYBOARD_EVENT;
    out.writeUInt32LE(0, 1);
    Buffer.from(encoded).copy(out, 5);
    this.broadcast(out);
  }

  broadcastStatus(status) {
    this.broadcastMeta('status', { data: status });
  }

  broadcastWeather(payload) {
    this.latestWeatherPayload = payload;
    this.broadcastMeta('weather', { data: payload });
  }

  broadcastClock(payload) {
    this.latestClockPayload = payload;
    this.broadcastMeta('clock', { data: payload });
  }

  broadcastMeta(type, body) {
    const text = JSON.stringify({ type, ...body });
    this.broadcast(text);
  }

  sendMeta(socket, type, body) {
    this.safeSend(socket, JSON.stringify({ type, ...body }));
  }

  safeSend(socket, data) {
    if (socket.readyState !== socket.OPEN) {
      return;
    }
    try {
      socket.send(data);
    } catch (err) {
      console.warn(`[bus] send failed: ${err.message}`);
    }
  }

  broadcast(data) {
    for (const client of this.wss.clients) {
      this.safeSend(client, data);
    }
  }
}
