import { EventEmitter } from 'node:events';

import { getPlatform } from './platform/index.mjs';

/*
 * Foreground-application watcher. Polls the platform adapter for the foreground
 * window's owning process + title and emits 'change' { process, title } when
 * either changes. This is the sensor the per-application context engine (F5)
 * keys off — switch app, reshape the device.
 *
 * Polling (not an event hook) is deliberate: no native addon, survives focus
 * storms with a debounce, and a ~600 ms cadence is imperceptible while cheap.
 * The OS-specific query lives in the platform adapter, so this is identical on
 * Windows / Linux / macOS.
 */
export class Foreground extends EventEmitter {
  constructor({ intervalMs = 600 } = {}) {
    super();
    this.intervalMs = intervalMs;
    this.timer = undefined;
    this.running = false;
    this.current = { process: '', title: '' };
  }

  start() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.poll();
    console.log('[foreground] watcher started');
  }

  stop() {
    this.running = false;
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
  }

  snapshot() {
    return { ...this.current };
  }

  async poll() {
    if (!this.running) {
      return;
    }

    try {
      const platform = await getPlatform();
      const info = await platform.foregroundApp();
      if (info && (info.process !== this.current.process || info.title !== this.current.title)) {
        const previous = this.current;
        this.current = info;
        this.emit('change', info, previous);
      }
    } catch {
      /* transient query failures are ignored; next tick retries */
    }

    if (this.running) {
      this.timer = setTimeout(() => this.poll(), this.intervalMs);
    }
  }
}
