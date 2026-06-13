import { UsbComm } from './protoLoader.mjs';

/*
 * Function-slot trigger drain loop, in the 中枢. Polls the keyboard board for
 * FUNCTION_SLOT_TRIGGER_EVENT_GET and runs the resulting helper actions
 * in-process — so a slot bound to a helper action (open app, run command, inject
 * key, ...) fires whether or not the config web page is open. Previously this
 * loop lived in the Vue store, so actions only ran while the page was open.
 */
export class Slots {
  constructor({ keyboardBoard, executeEvents, intervalMs = 700 }) {
    this.keyboardBoard = keyboardBoard;
    this.executeEvents = executeEvents;
    this.intervalMs = intervalMs;
    this.lastSeq = 0;
    this.running = false;
    this.timer = undefined;
  }

  start() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.poll();
    console.log('[slots] function-slot trigger drain started');
  }

  stop() {
    this.running = false;
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
  }

  async poll() {
    if (!this.running) {
      return;
    }

    try {
      if (this.keyboardBoard?.isConnected()) {
        const d2h = await this.keyboardBoard.send({
          action: UsbComm.Action.FUNCTION_SLOT_TRIGGER_EVENT_GET,
          functionSlotRequest: { afterSeq: this.lastSeq },
        });

        const events =
          (d2h.payload === 'functionSlotEvents' && d2h.functionSlotEvents?.events) || [];
        if (events.length) {
          const mapped = events.map((e) => ({
            seq: Number(e.seq || 0),
            slotIndex: Number(e.slotIndex || 0),
            actionCode: Number(e.actionCode || 0),
            arg0: Number(e.arg0 || 0),
          }));
          await this.executeEvents(mapped);
          this.lastSeq = Math.max(this.lastSeq, ...mapped.map((e) => e.seq));
        }
      }
    } catch {
      /* transient (board busy / disconnected); next tick retries */
    }

    if (this.running) {
      this.timer = setTimeout(() => this.poll(), this.intervalMs);
    }
  }
}
