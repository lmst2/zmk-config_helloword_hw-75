import { UsbComm } from './protoLoader.mjs';

/*
 * Per-application context engine — the heart of HW-75 as a context-aware desk
 * system. It watches the foreground app (via Foreground), matches it against a
 * data-driven rules table, and reshapes the device — knob feel, knob detents,
 * keyboard RGB theme, e-ink page — through the 中枢's HID sessions. Switch app,
 * the device follows. Uninstalled apps are simply absent rows (no match -> the
 * default rule, or nothing).
 *
 * Levers map to verified firmware actions:
 *   knob.feel    -> KNOB_SET_FEEL  (dynamic)  arbitrary mode/ppr/torque
 *   knob.detents -> KNOB_SET_DETENTS (dynamic) notched list/volume + endstops
 *   rgb          -> RGB_SET_STATE   (keyboard) app theme color/effect
 *   eink         -> EINK_SET_ACTIVE (dynamic)  per-app screen slot
 */

const KNOB_MODE = {
  disable: 0, inertia: 1, encoder: 2, spring: 3, damped: 4, spin: 5, ratchet: 6, switch: 7,
};

export class ContextEngine {
  constructor({ dynamic, keyboardBoard, foreground }) {
    this.dynamic = dynamic;
    this.keyboardBoard = keyboardBoard;
    this.foreground = foreground;
    this.rules = [];
    this.appliedKey = undefined;
    this.enabled = true;
  }

  setRules(rules) {
    this.rules = Array.isArray(rules) ? rules : [];
    this.appliedKey = undefined; // force re-apply on next focus
  }

  setEnabled(enabled) {
    this.enabled = !!enabled;
    if (this.enabled) {
      this.appliedKey = undefined;
    }
  }

  start() {
    this.foreground.on('change', (info) => {
      this.onForeground(info).catch((e) => console.warn(`[engine] ${e.message}`));
    });
    console.log(`[engine] context engine ready (${this.rules.length} rules)`);
  }

  /* Returns the first rule whose process (and optional title regex) matches, or
   * the rule flagged default. */
  matchRule(info) {
    const proc = (info.process || '').toLowerCase();
    for (const rule of this.rules) {
      if (rule.default) {
        continue;
      }
      const procs = (rule.match?.process || []).map((p) => String(p).toLowerCase());
      if (!procs.includes(proc)) {
        continue;
      }
      const re = rule.match?.titleRegex;
      if (re && !new RegExp(re, 'i').test(info.title || '')) {
        continue;
      }
      return rule;
    }
    return this.rules.find((rule) => rule.default);
  }

  async onForeground(info) {
    if (!this.enabled) {
      return;
    }
    const rule = this.matchRule(info);
    if (!rule) {
      return;
    }
    const key = rule.id || (rule.match?.process || []).join(',') || 'default';
    if (key === this.appliedKey) {
      return; // diff: the active scene is unchanged
    }
    this.appliedKey = key;
    console.log(`[engine] -> "${rule.id || key}" for ${info.process}`);
    await this.applyScene(rule.scene || {});
  }

  async applyScene(scene) {
    if (scene.knob) {
      await this.applyKnob(scene.knob);
    }
    if (scene.rgb) {
      await this.applyRgb(scene.rgb);
    }
    if (scene.eink !== undefined && scene.eink !== null) {
      await this.applyEink(scene.eink);
    }
  }

  async applyKnob(knob) {
    try {
      if (knob.detents) {
        await this.dynamic.send({
          action: UsbComm.Action.KNOB_SET_DETENTS,
          knobDetents: {
            count: knob.detents.count,
            strength: knob.detents.strength ?? 50,
            endstops: !!knob.detents.endstops,
          },
        });
      } else if (knob.feel) {
        const mode = typeof knob.feel.mode === 'string'
          ? (KNOB_MODE[knob.feel.mode] ?? KNOB_MODE.encoder)
          : (knob.feel.mode ?? KNOB_MODE.encoder);
        await this.dynamic.send({
          action: UsbComm.Action.KNOB_SET_FEEL,
          knobFeel: { mode, ppr: knob.feel.ppr ?? 0, strength: knob.feel.strength ?? 50 },
        });
      }
    } catch (e) {
      console.warn(`[engine] knob: ${e.message}`);
    }
  }

  async applyRgb(rgb) {
    try {
      const state = { on: rgb.on !== false };
      if (rgb.hsb) {
        state.color = { h: rgb.hsb.h | 0, s: rgb.hsb.s | 0, b: rgb.hsb.b | 0 };
      }
      if (rgb.effect !== undefined) {
        state.effect = rgb.effect | 0;
      }
      if (rgb.speed !== undefined) {
        state.speed = rgb.speed | 0;
      }
      await this.keyboardBoard.send({ action: UsbComm.Action.RGB_SET_STATE, rgbState: state });
    } catch (e) {
      console.warn(`[engine] rgb: ${e.message}`);
    }
  }

  async applyEink(index) {
    try {
      await this.dynamic.send({
        action: UsbComm.Action.EINK_SET_ACTIVE,
        einkActive: { activeIndex: index | 0 },
      });
    } catch (e) {
      console.warn(`[engine] eink: ${e.message}`);
    }
  }
}
