import { EventEmitter } from 'node:events';

import { getPlatform } from './platform/index.mjs';

/*
 * Now-Playing reader. Polls the platform adapter for the current media session
 * (title / artist / status / source app) and emits 'change' when any of them
 * changes. Feeds the e-ink Now-Playing card and lets the context engine know
 * media is playing. The OS-specific query lives in the platform adapter
 * (Windows SMTC, Linux MPRIS/playerctl, macOS Music/Spotify).
 */
export class Media extends EventEmitter {
  constructor({ intervalMs = 1500 } = {}) {
    super();
    this.intervalMs = intervalMs;
    this.timer = undefined;
    this.running = false;
    this.current = { title: '', artist: '', status: '', app: '' };
  }

  start() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.poll();
    console.log('[media] now-playing watcher started');
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
      const info = await platform.nowPlaying();
      if (info && (info.title !== this.current.title || info.artist !== this.current.artist ||
                   info.status !== this.current.status || info.app !== this.current.app)) {
        this.current = info;
        this.emit('change', info);
      }
    } catch {
      /* ignore transient failures */
    }

    if (this.running) {
      this.timer = setTimeout(() => this.poll(), this.intervalMs);
    }
  }
}
