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

export class Bus {
  constructor({ httpServer, keyboard, coreConfig, weather }) {
    this.keyboard = keyboard;
    this.coreConfig = coreConfig;
    this.weather = weather;
    this.latestWeatherPayload = undefined;
    this.latestClockPayload = undefined;

    this.wss = new WebSocketServer({ server: httpServer, path: '/ws' });
    this.wss.on('connection', (socket) => this.handleConnection(socket));

    keyboard.on('message', (message) => this.broadcastKeyboardEvent(message));
    keyboard.on('status', (status) => this.broadcastStatus(status));

    coreConfig.on('change', (config) => this.broadcastMeta('core-config', { config }));
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
    if (!this.keyboard.isConnected()) {
      throw new Error('Keyboard not connected');
    }
    const d2h = await Promise.race([
      this.keyboard.send(h2d),
      new Promise((_, reject) =>
        setTimeout(() => reject(new Error('Helper-core forward timeout')), META_SEND_TIMEOUT_MS)),
    ]);
    this.replyResponse(socket, requestId, d2h);
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
