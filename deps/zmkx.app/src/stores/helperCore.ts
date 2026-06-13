import { ref } from 'vue';
import { defineStore } from 'pinia';

import { UsbComm } from '@/proto/comm.proto';

const HELPER_WS_URL = 'ws://127.0.0.1:8755/ws';
const REQUEST_TIMEOUT_MS = 2500;
const RECONNECT_DELAY_MS = 1000;

// Binary WS frame types — wire contract; must match tools/hw75-helper/src/bus.mjs.
const FRAME_SEND_REQUEST = 0x01;
const FRAME_SEND_RESPONSE = 0x02;
const FRAME_SEND_ERROR = 0x03;
const FRAME_KEYBOARD_EVENT = 0x04;

export type HelperCoreWeather = {
  tempC: number;
  tempDeci: number;
  icon: number;
  city?: string;
  fetchedAt: number;
  weatherCode: number;
};

export type HelperCoreClock = {
  hour: number;
  minute: number;
  day: number;
  month: number;
  weekday: number;
  year: number;
  ts: number;
};

export type HelperCoreConfig = {
  weather: {
    enabled: boolean;
    provider: string;
    lat: number;
    lon: number;
    city: string;
    units: string;
    refresh_minutes: number;
  };
  clock: {
    enabled: boolean;
    refresh_minutes: number;
  };
};

type PendingRequest = {
  resolve: (d2h: UsbComm.MessageD2H) => void;
  reject: (err: Error) => void;
  timer: number;
};

type KeyboardEventListener = (d2h: UsbComm.MessageD2H) => void;

export const useHelperCore = defineStore('helperCore', () => {
  const connected = ref(false);
  const keyboardConnected = ref(false);
  const coreConfig = ref<HelperCoreConfig | null>(null);
  const lastWeather = ref<HelperCoreWeather | null>(null);
  const lastClock = ref<HelperCoreClock | null>(null);
  const lastError = ref<string | null>(null);

  let ws: WebSocket | null = null;
  let reconnectTimer: number | undefined;
  let nextId = 1;
  const pending = new Map<number, PendingRequest>();
  const keyboardEventListeners = new Set<KeyboardEventListener>();

  function scheduleReconnect() {
    if (reconnectTimer !== undefined) {
      return;
    }
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = undefined;
      connect();
    }, RECONNECT_DELAY_MS);
  }

  function connect() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return;
    }

    try {
      ws = new WebSocket(HELPER_WS_URL);
    } catch (err) {
      lastError.value = err instanceof Error ? err.message : String(err);
      scheduleReconnect();
      return;
    }

    ws.binaryType = 'arraybuffer';

    ws.addEventListener('open', () => {
      connected.value = true;
      lastError.value = null;
    });

    ws.addEventListener('close', () => {
      connected.value = false;
      keyboardConnected.value = false;
      failAllPending(new Error('Helper-core disconnected'));
      scheduleReconnect();
    });

    ws.addEventListener('error', (ev) => {
      lastError.value = 'WebSocket error';
      void ev;
    });

    ws.addEventListener('message', (ev) => {
      if (ev.data instanceof ArrayBuffer) {
        handleBinary(new Uint8Array(ev.data));
      } else if (typeof ev.data === 'string') {
        handleText(ev.data);
      }
    });
  }

  function failAllPending(err: Error) {
    for (const [, entry] of pending) {
      window.clearTimeout(entry.timer);
      entry.reject(err);
    }
    pending.clear();
  }

  function handleBinary(buf: Uint8Array) {
    if (buf.length < 5) {
      return;
    }
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    const frameType = view.getUint8(0);
    const requestId = view.getUint32(1, true);
    const payload = buf.subarray(5);

    if (frameType === FRAME_SEND_RESPONSE) {
      const entry = pending.get(requestId);
      if (!entry) {
        return;
      }
      pending.delete(requestId);
      window.clearTimeout(entry.timer);
      try {
        const d2h = UsbComm.MessageD2H.decodeDelimited(payload);
        entry.resolve(d2h);
      } catch (err) {
        entry.reject(err instanceof Error ? err : new Error(String(err)));
      }
    } else if (frameType === FRAME_SEND_ERROR) {
      const entry = pending.get(requestId);
      if (!entry) {
        return;
      }
      pending.delete(requestId);
      window.clearTimeout(entry.timer);
      const message = new TextDecoder().decode(payload);
      entry.reject(new Error(message || 'Helper-core error'));
    } else if (frameType === FRAME_KEYBOARD_EVENT) {
      try {
        const d2h = UsbComm.MessageD2H.decodeDelimited(payload);
        for (const listener of keyboardEventListeners) {
          listener(d2h);
        }
      } catch {
        /* ignore malformed event frames */
      }
    }
  }

  function handleText(text: string) {
    let msg: Record<string, unknown>;
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }

    const type = msg?.type;
    if (type === 'hello') {
      const kb = msg.keyboard as { connected?: boolean } | undefined;
      keyboardConnected.value = !!kb?.connected;
      coreConfig.value = (msg.config as HelperCoreConfig | undefined) ?? null;
      lastWeather.value = (msg.lastWeather as HelperCoreWeather | null) ?? null;
      lastClock.value = (msg.lastClock as HelperCoreClock | null) ?? null;
    } else if (type === 'status') {
      const data = msg.data as { connected?: boolean } | undefined;
      keyboardConnected.value = !!data?.connected;
    } else if (type === 'core-config') {
      coreConfig.value = (msg.config as HelperCoreConfig | undefined) ?? coreConfig.value;
    } else if (type === 'weather') {
      lastWeather.value = (msg.data as HelperCoreWeather) ?? null;
    } else if (type === 'clock') {
      lastClock.value = (msg.data as HelperCoreClock) ?? null;
    } else if (type === 'error') {
      lastError.value = String(msg.error ?? 'unknown');
    }
  }

  async function sendViaCore(h2d: UsbComm.IMessageH2D): Promise<UsbComm.MessageD2H> {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      throw new Error('Helper-core not connected');
    }

    const requestId = nextId++;
    const body = UsbComm.MessageH2D.encodeDelimited(h2d).finish();
    const frame = new Uint8Array(5 + body.length);
    frame[0] = FRAME_SEND_REQUEST;
    new DataView(frame.buffer).setUint32(1, requestId, true);
    frame.set(body, 5);

    const promise = new Promise<UsbComm.MessageD2H>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        if (pending.delete(requestId)) {
          reject(new Error('Helper-core request timeout'));
        }
      }, REQUEST_TIMEOUT_MS);
      pending.set(requestId, { resolve, reject, timer });
    });

    ws.send(frame);
    return promise;
  }

  function onKeyboardEvent(cb: KeyboardEventListener): () => void {
    keyboardEventListeners.add(cb);
    return () => keyboardEventListeners.delete(cb);
  }

  function requestCoreConfig(): void {
    ws?.send(JSON.stringify({ type: 'get-core-config' }));
  }

  function updateCoreConfig(patch: Partial<HelperCoreConfig>): void {
    ws?.send(JSON.stringify({ type: 'set-core-config', config: patch }));
  }

  function refreshWeatherNow(): void {
    ws?.send(JSON.stringify({ type: 'weather-refresh' }));
  }

  connect();

  return {
    connected,
    keyboardConnected,
    coreConfig,
    lastWeather,
    lastClock,
    lastError,
    sendViaCore,
    onKeyboardEvent,
    requestCoreConfig,
    updateCoreConfig,
    refreshWeatherNow,
  };
});
