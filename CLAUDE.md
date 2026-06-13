# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Read AGENTS.md first

**`AGENTS.md` is the canonical, detailed agent guide and is kept in sync with the source.** It covers the directory map, every subsystem's entry points, the protocol sync tables, change "recipes", and the list of already-hit pitfalls. Read it before any non-trivial change. This file is only a fast orientation layer; when the two disagree, **source code wins, then AGENTS.md, then this file.**

The `MIGRATION_*.md` and `HANDOFF_*.md` files are **historical decision logs only** — useful for "why is it this way", never authoritative about current state.

## What this repo is

A single repo that builds the firmware *and* companion app for the HelloWord **HW-75** modular keyboard. It is simultaneously:

- a **Zephyr module / ZMK config tree** (`config/` — board definitions, custom drivers, app-layer C code, protobuf);
- a **vendored, locally-patched ZMK fork** (`deps/zmk/` — CI and local builds compile *this*, not upstream `xingrz/zmk`);
- a **vendored Vue 3 + Pinia companion web app** (`deps/zmkx.app/`);
- a **Windows helper service** (`tools/hw75-core/`, evolving into a "helper-core" that proxies HID over WebSocket on `127.0.0.1:8755`).

Two hardware targets share one codebase but expose different feature sets:

| Board | MCU | Role | Owner's real unit |
| --- | --- | --- | --- |
| `hw75_keyboard@1.1` / `@1.2` | STM32F103XB | 82-key board + TouchBar | **`@1.2`** |
| `hw75_dynamic@A` / `@B` | STM32F405XG | detachable knob + e-ink module | **`@B`** |

Per-board feature sets are selected in each board's `Kconfig.usb` (`select HW75_USB_COMM_FEATURE_*`) and reported to the host in `Version.Features`, which in turn drives **which sidebar pages the web app shows**. Day-to-day dev targets `keyboard@1.2` + `dynamic@B`; other revisions only need to keep compiling in CI.

## Build & run

This is a **Windows / PowerShell** dev environment. Firmware needs `protoc`, `dtc`, `ninja`, `cmake`, and Python 3.12 on `PATH`.

```powershell
# Firmware — keyboard @1.2 (-> build/keyboard12/zephyr/zmk.uf2)
py -3.12 -m west build -p always -s deps/zmk/app -d build\keyboard12 `
    -b hw75_keyboard@1.2 `
    -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" `
       "-DKEYMAP_FILE=E:/code/zmk-config_helloword_hw-75/config/hw75_keyboard.keymap"

# Firmware — dynamic @B (-> build/dynamicB/zephyr/zmk.uf2)
py -3.12 -m west build -p always -s deps/zmk/app -d build\dynamicB `
    -b hw75_dynamic@B `
    -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" `
       "-DKEYMAP_FILE=E:/code/zmk-config_helloword_hw-75/config/hw75_dynamic.keymap"
```

Other revisions: swap `-b` and `-d` from the same template. CI (`.github/workflows/build.yml`) builds all 4 boards in a matrix from `deps/zmk/app` with `ZMK_CONFIG` pointing at `config/`; it pins `protobuf==4.25.3` (nanopb 0.4.x plugin requires protobuf Python `<5`).

```powershell
# Companion web app
cd deps/zmkx.app
npm ci            # postinstall runs build:proto + build:keyboard
npm run dev       # vite dev server on :8080
npm run build     # -> deps/zmkx.app/dist

# One-shot dev: starts hw75-core (:8755) + vite dev (:8080) with health checks
./start-hw75-dev.ps1
```

There is **no test suite**. "Verified" means: firmware `west build` succeeds *and* SRAM/FLASH watermarks didn't jump (see below); web app `npm run build` succeeds and `npm run dev` loads without console errors.

## Architecture: the cross-cutting rules

Most of the difficulty here is not in any single file — it's in invariants that span firmware ⇄ proto ⇄ web app ⇄ helper. Internalize these:

**1. `config/proto/usb_comm.proto` is the single source of truth for host↔device comms** (package `usb.comm`; top-level `MessageH2D` / `MessageD2H`, each an `action` + `oneof payload`). The firmware generates nanopb (`usb_comm.pb.{c,h}`); the web app and helper read the same `.proto` via protobufjs. **Changing any enum/field/oneof ripples to every layer at once** — firmware `config/app/usb_comm/handler/handler_*.c`, regenerated `deps/zmkx.app/src/proto/comm.proto.*` (`npm run build:proto`), the Pinia `stores/*.ts`, `debug_decode.ts`, the `routes/*.vue`, and (for a new feature) the `Version.Features` bit in `handler_version.c`. AGENTS.md §4.2 has the enum sync table; **never reuse an old enum ID — always append.**

**2. A new USB Action is a 3-part change**: add `Action.XXX` + payloads to `usb_comm.proto`; implement `handler_xxx.c` with `USB_COMM_HANDLER_DEFINE(Action_XXX, MessageD2H_payload_xxx_tag, handle_xxx)`; wire it in `handler/CMakeLists.txt` (`zephyr_library_sources_ifdef` on a Kconfig feature). Handlers are discovered via an iterable linker section, signature `bool handle_xxx(const H2D*, D2H*, const void* bytes, uint32_t len)`. Prefer extending an existing message/store over adding an Action.

**3. keyboard SRAM is critically tight (~98.5%, margin ~300 B).** Before adding *any* static buffer, thread stack, or oneof payload on the keyboard side, build and check the watermark — if it overflows, revert immediately. Two nanopb tools (configured per-board in `usb_comm.keyboard.options` / `usb_comm.dynamic.options`, staged to `usb_comm.options` at build time) keep the shared `.proto` from bloating the keyboard:
   - **`FT_IGNORE`** removes a peer-board-only oneof payload from this board entirely (no union slot, no handler) — so dynamic-only eink/knob fields cost the keyboard zero bytes.
   - **`FT_CALLBACK`** demotes a `repeated`/large nested field to an 8-byte `pb_callback_t`, streamed from a handler's static view (see `handler_touchbar.c`, `handler_function_slot.c`). Default new keyboard-side large payloads to callback, never `FT_STATIC`.

   `MessageH2D`/`MessageD2H` instances are `static` in `.bss` (`usb_comm_proto.c`) — **never put them on the stack** (1 KB thread stack will overflow).

**4. The web app must serialize all requests** through `stores/usb.ts::send` (`requestQueue` + single `activePendingRequest`); the firmware has a single RX slot and a shared TX buffer. **Never call `comm.send` directly from a Vue component** — always go through the relevant `stores/*.ts`. Timeouts (1500 ms) are `Promise.reject`, not silent failures — handle with `try/catch`.

**5. Feature-gating drives visibility**: a new sidebar page only appears if its `Version.Features.*` bit is set in `handler_version.c` *and* selected via Kconfig for that board.

**6. HID device slots**: `HID_0` is ZMK's keyboard; the TouchBar mouse uses `HID_2` (`CONFIG_USB_HID_DEVICE_COUNT=3`). Don't reassign these.

## Subsystem map (entry points — don't reinvent)

| Subsystem | Firmware entry | Notes |
| --- | --- | --- |
| Diagnostic log | `hw75_diag_log_event()` / `hw75_diag_update_snapshot()` (`config/app/diag_log.c/.h`) | ring buffer; host polls via `LOG_GET_STATE`/`LOG_GET_EVENTS` |
| RGB | `handler_rgb.c` (thin proto) + `config/app/rgb_effects.c` (custom effects via `deps/zmk` `__weak` `zmk_rgb_underglow_custom_effect_*` hooks) | keyboard layout = 103 LEDs (1.1 = 101); status LEDs only via `indicator.c` |
| TouchBar | `config/app/touchbar.c` (6-channel gesture state machine, keyboard-only) | raw state from `kscan_gpio_74hc165.c`; keymap uses `&tb_mode`; mouse out via `hid_mouse.c` |
| Function Slot | `config/app/function_slot.c` (5 slots) | keymap `&fn_slot <idx>`; helper actions bridge to `tools/hw75-core` |
| Dynamic knob | `config/boards/arm/hw75_dynamic/app/knob_app.c` + `config/drivers/sensor/knob/` profiles | host `KNOB_GET/SET_CONFIG`, calibration |
| Dynamic e-ink / OLED | `eink_app.c`, `drivers/display/ssd16xx.c`, LVGL `screen/` | `EINK_SET_IMAGE` (max 8192-byte `bytes`); fonts subset at build via `cmake/lv_font_conv.cmake` |
| keyboard↔dynamic link | `config/drivers/console/uart_slip.c` + `uart_comm.proto` (separate from usb_comm) | `uart_slip_send` is non-blocking with a 20 ms total budget — see AGENTS.md pitfall #11; do **not** revert to busy-wait |

Keymap layers: keyboard = `BASE` / `FN` / `Touch`; dynamic uses sensor-binding style.

## Conventions

- **Reuse → extend → write new, in that order.** Grep for existing symbols/handlers before adding code. Only refactor when necessary; don't "optimize" untouched code.
- After deleting a function/enum/proto field, check the *other* ends (firmware ↔ web app ↔ helper) for dangling references — they compile fine but fail at runtime.
- A proto change is incomplete until *both* generated products are rebuilt: `usb_comm.pb.{c,h}` (firmware build) and `comm.proto.{js,d.ts}` (`npm run build:proto`). Keymap default changes that affect the Keyboard page need `npm run build:keyboard`.
- Keep `AGENTS.md` in sync when source changes — update the affected section only, no broad rewrites.
