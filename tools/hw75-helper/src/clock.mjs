import { UsbComm } from './protoLoader.mjs';

export class Clock {
  constructor({ keyboard, coreConfig, bus }) {
    this.keyboard = keyboard;
    this.coreConfig = coreConfig;
    this.bus = bus;
    this.timer = undefined;
  }

  start() {
    this.scheduleNext();
  }

  stop() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
  }

  scheduleNext() {
    if (this.timer) {
      clearTimeout(this.timer);
    }
    /* Wake up at the next minute boundary so pushed clock frames align with the
     * wall clock exactly.
     */
    const now = new Date();
    const delay = (60 - now.getSeconds()) * 1000 - now.getMilliseconds();
    this.timer = setTimeout(() => this.tick(), Math.max(100, delay));
  }

  async tick() {
    const cfg = this.coreConfig.snapshot().clock;
    if (cfg.enabled !== false) {
      const now = new Date();
      const payload = {
        hour: now.getHours(),
        minute: now.getMinutes(),
        day: now.getDate(),
        month: now.getMonth() + 1,
        weekday: now.getDay(),
        year: now.getFullYear(),
        ts: now.getTime(),
      };
      this.bus?.broadcastClock(payload);

      if (this.keyboard.isConnected()) {
        try {
          await this.keyboard.send({
            action: UsbComm.Action.EINK_PUSH_CLOCK,
            einkClock: {
              hour: payload.hour,
              minute: payload.minute,
              day: payload.day,
              month: payload.month,
              weekday: payload.weekday,
              year: payload.year,
            },
          });
        } catch (err) {
          console.warn(`[clock] push failed: ${err.message}`);
        }
      }
    }

    this.scheduleNext();
  }
}
