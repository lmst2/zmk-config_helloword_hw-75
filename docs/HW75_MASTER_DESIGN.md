# HW-75 「中枢」 Master Design — context-aware desk system

Status: design locked 2026-06-13. This is the single global spec the whole build hangs off.
Scope: do the entire vision — PC host app + both firmwares + web frontend + both comm channels —
as ONE coherent system. No feature dropped. The only external dependency is one hardware fact
(keyboard `usart1` RX trace) that gates the reverse-UART subset; everything else proceeds.

---

## 1. Vision

HW-75 becomes a **context-aware desk system**:
- **中枢** (the PC host app, renamed from `hw75-core`) is the brain — it senses what you're
  doing (foreground app, media, system state) and orchestrates both boards.
- The two boards **also collaborate directly over UART**, so the device stays alive and
  expressive **with no PC attached** (haptic feedback to typing, TouchBar-as-remote, activity wake).

## 2. Topology

```
              ┌──────────── PC：中枢 (host app) ─────────────────┐
              │ foreground-app watcher · rules/scenes engine ·   │
              │ SMTC media · inject-key · e-ink rasterizer ·     │
              │ owns BOTH USB-HID sessions (keyboard + dynamic)  │
              │                 WS  ◄──►  web (pure frontend)    │
              └────┬──────────────────────────────┬─────────────┘
              USB-HID(keyboard)              USB-HID(dynamic)
                   │                              │
          ┌────────▼────────┐   UART(kbd→dyn)   ┌─▼──────────────┐
          │  KEYBOARD F103  │ ─────────────────►│  DYNAMIC F405   │
          │ keys/TouchBar/  │  (reverse needs    │ knob/e-ink/OLED │
          │ 103 RGB         │   usart1 RX trace) │ 4 RGB           │
          └─────────────────┘                   └────────────────┘
```

## 3. Three integration planes

| Plane | Path | Needs PC? | Carries |
| --- | --- | --- | --- |
| **A. PC plane** | 中枢 ⇄ each board (USB-HID `usb_comm`); 中枢 ⇄ web (WS) | yes | per-app context engine, media cards, programmable haptics, all rich features |
| **B. Local plane** | keyboard → dynamic (`uart_comm`, one-way today) | no | haptic bump on key events, TouchBar-as-remote, activity wake, telemetry |
| **C. Reverse local** | dynamic → keyboard (`uart_comm` MessageD2K) | no | knob/OLED as on-device configurator — **GATED on keyboard usart1 RX trace** |

## 4. The global principle: one primitive, two drivers

Every capability is a firmware **internal primitive** implemented ONCE, callable from BOTH the
USB handler (PC) and the UART handler (local). Never two parallel systems.

| Internal primitive (firmware) | Called by USB (`usb_comm`) | Called by local (`uart_comm`) |
| --- | --- | --- |
| `knob_pulse(strength,count)` — transient torque override + `k_work` restore | `KNOB_PULSE` (中枢: new-message/mute-confirm) | layer-change / caps / slot-fired bump |
| `knob_set_detents(count,strength,endstops)` | `KNOB_SET_DETENTS` (中枢: per-app dial) | OLED "menu" mode |
| `eink_render_card()` + `EINK_PUSH_FRAME` (host-rasterized 1-bit) | media/status cards | local stats/cheatsheet mode |
| scene apply `{knob profile, eink mode, rgb}` | context engine | knob-selected local scene |

> Implementation note: the knob has no VOLTAGE mode and its tick loop rewrites `motor_control`
> every ~200 µs, so `knob_pulse` is a **transient override** (brief TORQUE kick or small ANGLE
> offset held ~40–80 ms by `k_work_delayable`), then restore via `knob_app_apply_pref(current_layer)`.

## 5. Naming

Brand **中枢** (Zhōngshū, "nerve center"); pairs with 瀚文 ("瀚文中枢"). In code, retire the word
`helper`; keep the existing `core` link-layer convention. `hw75-core` → host app package;
`helperCore.ts` → `coreClient.ts` / `useCore`; wire-contract strings (port 8755, `/ws`, `/api/*`,
frame bytes 0x01–0x04, version) change on both ends in lockstep; migrate `%APPDATA%\hw75-core`.

## 6. Unified protocol additions

### 6.1 `usb_comm.proto` (PC ↔ dynamic; knob is dynamic-only)
- Actions: `KNOB_PULSE = 30`, `KNOB_SET_DETENTS = 31`.
- Messages `KnobPulse {strength,count,interval_ms}`, `KnobDetents {count,strength,endstops,wrap}`.
- `MessageH2D` oneof: `knob_pulse = 20`, `knob_detents = 21`.
- **keyboard `FT_IGNORE`** both new payloads (`usb_comm.keyboard.options`) — zero keyboard SRAM.
- Later: media is host-rasterized → reuse `EINK_PUSH_FRAME` (no new proto). `inject-key` and
  `SMTC` are 中枢-side only (no firmware/proto).

### 6.2 `uart_comm.proto` (keyboard → dynamic; builds full on both, no per-board options)
- Keep `FN_STATE_CHANGED` (load-bearing: forces dynamic layer). Add Actions:
  `LAYER_INDEX=3, CAPS_TOGGLED=4, TOUCHBAR_GESTURE=5, TOUCHBAR_MODE=6, ACTIVITY_HINT=7, SLOT_FIRED=8`.
- Messages: `LayerIndex{index}`, `CapsToggled{engaged}`, `TouchbarGesture{verb}` (0 swipe_l/1 swipe_r/2 tap/3 long),
  `TouchbarMode{mode}`, `ActivityHint{state,intensity}` (0 active/1 idle/2 sleep), `SlotFired{slot_index,slot_type}`.
- All payloads are 1–2 fields; emits are **edge-triggered** (respect 20 ms send budget + 3-fail/5-min breaker).

### 6.3 Reverse channel `MessageD2K` (dynamic → keyboard) — GATED
- Requires: keyboard `usart1` **RX pin in DTS + a physical PCB trace** (today pinctrl is TX-only).
- Requires: keyboard RX path that adds **NO resident thread** (296 B margin) — a non-blocking SLIP
  drain folded into the existing system workqueue, decode into a stack buffer, dispatch to an
  existing keyboard API (`zmk_rgb_underglow_*`, `touchbar_set_mode`, `hw75_function_slot_binding_pressed`).
- Built ONCE as infra, then unlocks: knob-menu sets keyboard RGB/TouchBar mode, knob-as-extra-keys.

## 7. 中枢 (host app) architecture

```
core/
  link/        node-hid sessions to BOTH boards (keyboard + dynamic), serialized per board
  bus/         WS server: proxy H2D/D2H + broadcast events to web
  sense/       foreground-window watcher, SMTC media reader, system metrics
  act/         inject-key, run-command, open-app/url, window-management
  render/      e-ink 1-bit rasterizer (templates → EINK_PUSH_FRAME)
  engine/      rules + scenes (the context engine)
  store/       profiles + rules + core-config (migrated from %APPDATA%)
```

**Context engine data model** (data-driven; uninstalled apps = absent rows):
```jsonc
Rule {
  match: { process: ["cloudmusic.exe", ...], titleRegex?: "..." },
  scene: {
    knob?:  { profile: "ENCODER" } | { detents: { count, strength, endstops } },
    eink?:  { mode } | { card: "now-playing" | "stats" | "<template>" },
    rgb?:   { hsb, effect },
    touchbar?: { mode },
    slots?: [ ... ],            // function-slot set
    pulseOn?: ["new-message"]   // KNOB_PULSE triggers
  }
}
```
Foreground change → first matching Rule → diff vs current applied state → push only changed levers.
A `default` rule and a `media-fallback` rule (SMTC) cover the unknown-app case.

## 8. Web app = pure frontend
- Migrate the 6 WebHID stores (`rgb/touchbar/knob/version/debug/function-slots`) from `stores/usb.ts`
  to `core.sendViaCore`; delete `stores/usb.ts` + `utils/usb/usb-hid.ts`.
- Move the function-slot trigger drain loop **into 中枢** (fixes "only fires while the web page is open").
- 中枢 opens the **second HID session to the keyboard** ("HW-75 Keyboard"); WS API gains board routing.

## 9. Build order (dependency-correct; drive straight through)

```
F0  Protocol keystone: usb_comm (KNOB_PULSE/DETENTS + kbd FT_IGNORE) + uart_comm (kbd→dyn msgs).  ← START
F1  Firmware shared primitives on dynamic: knob_pulse(), knob_set_detents(); USB handlers.
F2  Local plane (no PC): keyboard emitters (layer/caps/touchbar-gesture/activity/slot) + dynamic
    handlers → knob bump, TouchBar-as-remote, activity wake. (feature ①②③)
F3  中枢 foundation: rename, 2nd HID session (keyboard), board routing, foreground watcher,
    inject-key, SMTC, e-ink rasterizer.
F4  Web → pure frontend: migrate 6 stores, move drain loop into 中枢, delete usb.ts/usb-hid.ts.
F5  Context engine: rules+scenes model, the China-app rule set (网易云/B站/微信/WPS/剪映…), default+media-fallback.
F6  Reverse channel (GATED on usart1 RX trace): D2K transport + knob-as-configurator.
```

Each firmware step is build-verified on both boards (keyboard SRAM must stay ≤ 20384 B gate / 296 B margin).

## 10. The one open hardware question

Does the keyboard PCB route `usart1` RX to the dynamic connector? If **yes** → F6 is a firmware
infra project. If **no** → F6 waits for a hardware revision; F0–F5 deliver the full PC plane +
the local keyboard→dynamic plane regardless.
