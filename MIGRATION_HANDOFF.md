# HW-75 Migration Handoff

## Scope

Continue work in:

- `e:\code\zmk-config_helloword_hw-75`

Read this file first.
If earlier rationale is needed, then read:

- `MIGRATION_CONTEXT.md`

Do not redesign the project at a high level.
Continue from the current implementation.

## Debugging Discipline

Follow this rule for future transport work:

- do not treat a plausible hypothesis as the root cause without proving it
- do not land speculative fixes just because a symptom looks suspicious
- first trace the exact data flow and byte flow end-to-end
- verify where corruption first appears before changing behavior
- prefer instrumentation and narrowly targeted verification over workaround patches
- if a hypothesis is not yet proven, label it clearly as a hypothesis
- if a workaround is temporarily added for diagnosis, record that it is temporary and remove or revisit it once evidence arrives
- when a temporary diagnostic edit is added, persist it to handoff docs immediately so the reason survives context compression

Specific expectation for `LOG_GET_EVENTS`:

- locate the first stage that produces malformed bytes
- distinguish between firmware message construction, nanopb encode, HID packetization, host reassembly, and host decode
- prioritize root-cause fixes over memory-expensive buffering or host-side masking

## Current Priority

The immediate priority is no longer TouchBar behavior itself.
The immediate priority is:

- finish stabilizing the binary debug log transport on real hardware

Until the log chain is trustworthy again:

- do not debug TouchBar gesture logic
- do not redesign RGB
- do not do speculative memory cleanup

## Repository Shape

- ZMK is vendored in `deps/zmk`
- firmware source is `deps/zmk/app`
- config/module repo is `config`
- host app repo is vendored in `deps/zmkx.app`
- GitHub Actions build from local `deps/zmk`, not external forks

## Already Confirmed Good

User already confirmed these on hardware before the latest logging regression:

- keyboard typing works again
- keyboard RGB effects are visually correct again
- host app no longer shows dynamic-only `eink/knob` menus when keyboard is connected

## Important Previously Fixed Root Causes

### 1. Keyboard typing was broken

Root cause:

- `config/app/hid_mouse.c` previously re-registered `HID_0` as mouse
- `HID_0` is the main keyboard HID device

Fix already implemented:

- `CONFIG_USB_HID_DEVICE_COUNT=3`
- TouchBar mouse helper moved to `HID_2`

Key file:

- [hw75_keyboard_defconfig](/e:/code/zmk-config_helloword_hw-75/config/boards/arm/hw75_keyboard/hw75_keyboard_defconfig)

### 2. Host app showed dynamic-only menus

Root causes:

- keyboard USB comm feature payload used to exceed small transport buffers
- host app sidebar fallback logic treated missing features as dynamic defaults

Fix already implemented:

- keyboard USB comm buffers were enlarged
- host app only shows `eink/knob` when firmware explicitly reports them

Key files:

- [Kconfig.usb](/e:/code/zmk-config_helloword_hw-75/config/boards/arm/hw75_keyboard/Kconfig.usb)
- [Main.vue](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/pages/Main.vue)

### 3. TouchBar exported the wrong logical bits before

Old wrong behavior:

- `config/drivers/kscan/kscan_gpio_74hc165.c` exported TouchBar state from the last raw scan byte
- that was raw, pre-remap, and pre-debounce

Fix already implemented:

- driver now derives TouchBar state from current `matrix-transform` positions `82..87`
- uses debounced pressed state
- exports logical 6-bit state aligned with `config/app/touchbar.c`

Key file:

- [kscan_gpio_74hc165.c](/e:/code/zmk-config_helloword_hw-75/config/drivers/kscan/kscan_gpio_74hc165.c)

## Binary Debug Logging System

The old string logging was replaced with structured binary logs.

Current event model:

- `module`
- `level`
- `event_id`
- `trace_id`
- `data0`
- optional `data1`
- `repeat_count`

Current snapshot model:

- per-module `state0/state1/state2`
- host-side decoding into readable text

Main files:

- [diag_log.h](/e:/code/zmk-config_helloword_hw-75/config/app/include/app/diag_log.h)
- [diag_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/diag_log.c)
- [usb_comm.proto](/e:/code/zmk-config_helloword_hw-75/config/proto/usb_comm.proto)
- [handler_debug_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_debug_log.c)
- [debug.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug.ts)
- [debug_decode.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug_decode.ts)
- [Debug.vue](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/routes/Debug.vue)

## Latest Round: Debug Transport Regression And Partial Fix

### What was temporarily disabled earlier

To keep `LOG_GET_STATE` within one HID response while transport debugging was underway:

- `snapshots` were disabled in `LOG_GET_STATE`
- `boot_events` were also disabled in `LOG_GET_STATE`
- `LOG_GET_EVENTS` was temporarily forced down to one event per response
- host polling was also requesting only one event at a time

That temporary downgrade lived in:

- [handler_debug_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_debug_log.c)
- [debug.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug.ts)

### What was changed in the latest round

Host-side usability improvements:

- `Transport Trace` now exists in Debug page
- it is collapsed by default
- repeated trace entries are coalesced with `xN`
- stale half-decoded RX buffers are dropped before sending the next request
- transport trace is retained across disconnect until manually cleared, so freeze-before-reconnect evidence is not lost

Files:

- [usb.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/utils/usb/usb.ts)
- [usb-hid.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/utils/usb/usb-hid.ts)
- [stores/usb.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/usb.ts)
- [Debug.vue](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/routes/Debug.vue)

Transport throughput changes:

- host polling interval tightened
- host now requests `maxCount=4`
- firmware now allows up to `HW75_DIAG_LOG_EVENT_BATCH_MAX`

Files:

- [debug.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug.ts)
- [handler_debug_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_debug_log.c)

### Important latest regression

After re-enabling multi-event batches, real hardware showed:

- `LOG_GET_STATE` still decoded
- `LOG_GET_EVENTS` request was sent
- multi-packet event responses arrived
- host never emitted `rx-decode` for those `LOG_GET_EVENTS` responses
- RX queue kept growing because later packets and unrelated responses were concatenated onto one undecoded buffer

User-provided trace pattern looked like:

- `tx-message | TX LOG_GET_EVENTS ...`
- `rx-packet` with first 62-byte chunk
- `rx-packet` with follow-up 18-byte chunk
- no `rx-decode` for `logEvents`
- later `RGB_GET_STATE` / `RGB_GET_INDICATOR` responses were appended into the same queue
- Debug page then timed out waiting for `LOG_GET_STATE`

### Current root-cause hypothesis that was implemented as a fix

The most likely cause was in firmware callback encoding:

- `pb_get_encoded_size()` runs one full encode pass
- `pb_encode_delimited()` runs a second full encode pass
- `encode_log_events_callback()` was previously reading directly from the live diag ring on each pass
- if the ring changed between sizing and actual encoding, the length prefix and actual bytes could diverge
- that would produce an undecodable multi-event message even though single-event responses worked

Fix implemented:

- `handler_debug_log.c` now copies the selected `events`, `snapshots`, and `boot_events` into stable arrays inside the encoding context before nanopb starts encoding
- both sizing and actual encode now read the same frozen data

Key file:

- [handler_debug_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_debug_log.c)

## Current Real Status

At the time of this handoff:

- the above stable-buffer fix has been coded
- host stale-buffer mitigation has been coded
- both host and firmware build successfully
- this exact fix has not yet been re-verified on real hardware in this conversation

This means:

- do not assume the transport is fully fixed yet
- the next window should start from real-device verification of the logging chain itself

## Newest Real-Hardware Update

The latest local diagnostic firmware was confirmed to run on hardware using a temporary `VERSION.app_version` suffix:

- previous build: `+usbtxretry1`
- later build: `+usbtxcoop1`
- later build: `+usbtxready1`
- later build: `+usbtxrqoff1`
- later build: `+usbsharedoff1`
- current expected build for the next root-cause send-timeout retest: `+usbtxwait1`

Why this matters:

- the commit hash reported by `VERSION` was not enough to distinguish local uncommitted firmware edits
- the temporary suffix proved the latest local UF2 actually reached the keyboard

Important:

- this marker is temporary diagnostic scaffolding
- it lives in [handler_version.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_version.c)
- remove or revisit it after transport diagnosis is complete

Newest startup-path adjustment:

- [usb_comm_proto.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_proto.c) thread creation was changed back from `K_PRIO_PREEMPT(...)` to `K_PRIO_COOP(...)`
- reason: this was the narrowest recent startup-active USB/transport change that could plausibly affect post-flash boot behavior
- this rollback is a targeted test, not yet a proven root-cause fix

Newest generic TX-path adjustment:

- [usb_comm_hid.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_hid.c) request-path send logic was changed back away from active `hid_int_ep_write(...)/-EAGAIN` retry
- transmit gating now again uses `int_in_ready` callback plus semaphore, matching the local ZMK reference implementation in [usb_hid.c](/e:/code/zmk-config_helloword_hw-75/deps/zmk/app/src/usb_hid.c)
- reason: after the `K_PRIO_COOP(...)` rollback, the earliest visible failure appears even earlier and can prevent `VERSION` from showing at all, which implicates the generic response send path more directly than the debug-only request chain
- this is a targeted rollback of our own recent low-level transport change, not yet a proven root-cause fix

Newest shared log-path adjustment:

- [usb_comm_proto.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_proto.c) no longer emits generic `HW75_DIAG_EVENT_USB_REQUEST` records for normal USB requests in the next isolation build
- reason: code search confirmed RGB page does not send `LOG_*` requests, but the log system was still participating in shared request handling because `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR` all passed through this generic request logger
- this is a narrow diagnostic rollback of our own recent shared logging change, not yet a proven root-cause fix

Newest shared USB-layer isolation adjustment:

- [usb_comm_proto.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_proto.c) and [usb_comm_hid.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_hid.c) no longer write diag-log events/snapshots from the shared USB transport path in the next isolation build
- reason: latest host trace showed zero RX packets even for `VERSION`, so disabling only generic request logging was not enough; this stronger isolation removes remaining shared `usb_comm`-layer log-ring participation before suspecting untouched handlers
- this is a broader diagnostic rollback of our own shared logging edits, not yet a proven root-cause fix

Newest likely-event-source isolation adjustment:

- [hid_mouse.c](/e:/code/zmk-config_helloword_hw-75/config/app/hid_mouse.c) no longer emits the `HW75_DIAG_EVENT_HID_INIT` startup event in the next isolation build
- reason: after shared USB-layer isolation restored `VERSION` and `LOG_GET_STATE`, the remaining corruption appears concentrated in the first startup event of `LOG_GET_EVENTS`, and the HID init event is now the most plausible candidate produced by code we recently added
- HID snapshot update remains enabled, so this is narrower than removing HID support or redesigning startup

Newest host-side boundary adjustment:

- [debug.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug.ts) temporarily requests `LOG_GET_EVENTS` with `maxCount=1`
- reason: after `+hidhinitoff1`, `VERSION` and `LOG_GET_STATE` decode again but `LOG_GET_EVENTS` remains the next failing stage; this isolates single-event/single-packet behavior from multi-event/multi-packet behavior

Newest root-cause finding from code-path tracing:

- the apparent "later no RX" symptom can be downstream of a firmware-side stall, not only a host-side decode failure
- `usb_comm_thread` currently runs as `K_PRIO_COOP(...)`
- `usb_comm_hid_send()` ignores `k_sem_take(..., K_MSEC(30))` timeout and still calls `hid_int_ep_write()`
- Zephyr `usb_write()` retries `-EAGAIN` with `k_yield()`; in a cooperative thread that can monopolize CPU and make the keyboard appear dead
- next correction should therefore target that shared HID send-timeout path first, before chasing more surface-level `LOG_GET_STATE` / `LOG_GET_EVENTS` / RGB symptoms

Deeper root cause found after reviewing the latest `+usbtxwait1` trace and full request flow:

- firmware USB request intake is single-slot:
  - one shared `usb_rx_buf`
  - one shared `usb_rx_len`
  - one shared `usb_rx_idx`
  - one `k_sem` with max count `1`
- host currently overlaps requests from multiple pages/stores without any global request-response lock
- this mismatch is now the strongest root-cause candidate for the shifting `VERSION` / `RGB_*` / `LOG_*` failure boundary
- next correction should serialize host requests end-to-end before adding more firmware-side buffering or making more symptom-level protocol changes

Also newly confirmed on hardware:

- even with the confirmed latest diagnostic firmware, opening Debug still hard-freezes the keyboard
- the observed failure boundary is now earlier than before: `VERSION` still decodes, but `LOG_GET_STATE` may receive no response at all
- after changing thread creation back to `K_PRIO_COOP(...)`, UF2 copy behavior recovered, but opening the host app can now freeze the keyboard before `VERSION` visibly completes
- with the newer `+usbtxready1` firmware, `VERSION` was observed correctly at least once
- after that, attempting RGB effect changes froze the keyboard
- after disconnect/reconnect following that freeze, `VERSION` no longer came back reliably

Interpretation:

- do not assume the old "`LOG_GET_EVENTS` multi-packet corruption" boundary is still the first failure
- do not assume the current earliest boundary is still simply "before `LOG_GET_STATE`"
- first re-prove whether the first failing request after a healthy `VERSION` is `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`

## Build Status

### Keyboard build

Local Windows build succeeds.

Latest artifact:

- [keyboard uf2](/e:/code/zmk-config_helloword_hw-75/build/keyboard12/zephyr/zmk.uf2)

Latest observed memory usage after the latest shared USB-layer isolation build:

- `FLASH: 72320 B / 104 KB = 67.91%`
- `SRAM: 20184 B / 20 KB = 98.55%`

Important:

- SRAM is now extremely tight again
- the latest log transport fix intentionally prioritized correctness over memory margin
- do not start random memory optimization before first confirming whether the log chain now works

### Host app build

Local host build succeeds.

Built artifact:

- [zmkx.app dist index](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/dist/index.html)

Dev server expectation:

- `http://localhost:8080/`

## Immediate Next Step

Do not work on TouchBar behavior yet.
Do not restore snapshots yet.

Start from real hardware with the latest firmware and latest host app:

1. flash the newer firmware build that reports `VERSION.app_version` with temporary suffix `+usbtxwait1`
2. confirm keyboard still types normally before opening the host app
3. run latest host app from `deps/zmkx.app`
4. first verify whether `VERSION` receives any decodable response at all
5. if `VERSION` is healthy, next verify the RGB-side boundary because entering RGB page automatically sends `RGB_GET_STATE` and `RGB_GET_INDICATOR`
6. distinguish whether freeze first appears on those automatic RGB requests or only on a later `RGB_SET_STATE` effect-change request
7. only after that, open Debug page
8. click `Show Trace`
9. verify whether `LOG_GET_STATE` receives any response at all after `VERSION`
10. only if `LOG_GET_STATE` is healthy again, continue evaluating `LOG_GET_EVENTS`

If event transport is now healthy:

- next step is to decide whether to restore `boot_events` or `snapshots` first
- likely restore `boot_events` first, because it is lower risk than snapshots

If event transport is still broken:

- copy the new Transport Trace
- explicitly note whether `+usbtxwait1` was present in the `VERSION` response
- explicitly note whether `VERSION` got zero RX packets, malformed RX data, or a timeout
- explicitly note whether the first RGB-side failing request was `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`
- explicitly note whether `LOG_GET_STATE` got zero RX packets or got malformed RX data
- prioritize transport diagnosis only
- do not switch to TouchBar debugging

## Files Most Relevant In The Next Window

- [MIGRATION_HANDOFF.md](/e:/code/zmk-config_helloword_hw-75/MIGRATION_HANDOFF.md)
- [MIGRATION_HANDOFF_PROMPT.md](/e:/code/zmk-config_helloword_hw-75/MIGRATION_HANDOFF_PROMPT.md)
- [handler_debug_log.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/handler/handler_debug_log.c)
- [usb_comm_proto.c](/e:/code/zmk-config_helloword_hw-75/config/app/usb_comm/usb_comm_proto.c)
- [usb-hid.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/utils/usb/usb-hid.ts)
- [usb.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/utils/usb/usb.ts)
- [stores/usb.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/usb.ts)
- [debug.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug.ts)
- [Debug.vue](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/routes/Debug.vue)
- [debug_decode.ts](/e:/code/zmk-config_helloword_hw-75/deps/zmkx.app/src/stores/debug_decode.ts)

## Useful Commands

### Keyboard build

```powershell
$env:PATH='C:\Users\wujia\AppData\Local\Microsoft\WinGet\Packages\Google.Protobuf_Microsoft.Winget.Source_8wekyb3d8bbwe\bin;C:\Users\wujia\AppData\Local\Microsoft\WinGet\Packages\oss-winget.dtc_Microsoft.Winget.Source_8wekyb3d8bbwe\usr\bin;C:\Users\wujia\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Users\wujia\AppData\Roaming\npm;C:\Users\wujia\AppData\Local\Programs\Python\Python312\Scripts;C:\Users\wujia\AppData\Local\Programs\Python\Python312;C:\Program Files\CMake\bin;'+$env:PATH
py -3.12 -m west build -p always -s deps/zmk/app -d build\keyboard12 -b hw75_keyboard@1.2 -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" "-DKEYMAP_FILE=E:/code/zmk-config_helloword_hw-75/config/hw75_keyboard.keymap"
```

### Host app

```powershell
cd deps/zmkx.app
npm run dev
```

## 2026-04-05 RGB Wake Boundary Update

New hardware evidence changed the failure boundary:

- binary debug log transport is healthy after the host-side global request serialization fix
- the user noticed those healthy runs happened while RGB was not visibly lit
- board config has `CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=y`
- after idle sleep and a wake keypress, RGB lights again
- if the host app is started while RGB is already visibly active, the lighting freezes immediately and the keyboard hard-locks

So the current earliest proven trigger is:

- active RGB refresh + host USB traffic

Current narrowed hypothesis:

- the issue is likely in the shared path between underglow refresh and USB/log transport, not generic `LOG_*` framing itself
- `deps/zmk/app/src/rgb_underglow.c` performs full-strip refresh on every RGB tick
- local code tracing identified `irq_lock()` around `led_strip_update_rgb()` as the strongest shared-risk boundary once RGB is active

Temporary local diagnostic change staged:

- remove `irq_lock()/irq_unlock()` around `led_strip_update_rgb()` in `deps/zmk/app/src/rgb_underglow.c`
- temporary firmware marker: `+rgbirqoff1`

Treat this as diagnostic until hardware confirms whether the freeze disappears.

## 2026-04-05 RGB Handler Log Isolation

The `+rgbirqoff1` diagnostic did not resolve the freeze and has been backed out locally.

Current staged diagnostic now isolates only our own added RGB-side logging path:

- restore original `deps/zmk/app/src/rgb_underglow.c`
- remove RGB handler `hw75_diag_log_event()` / `hw75_diag_update_snapshot()` calls from `config/app/usb_comm/handler/handler_rgb.c`
- temporary firmware marker: `+rgblogoff1`

Rationale:

- `RGB_GET_STATE` succeeds before the freeze
- the failing path is the RGB mutation request path (`RGB_CONTROL` / `RGB_SET_STATE`)
- before blaming vendor RGB logic, isolate the diagnostic logging that was added in those handlers

## 2026-04-05 USB TX Failure Telemetry

`+rgblogoff1` still did not change the freeze.

Current staged diagnostic now moves to failure-only USB send telemetry:

- add `HW75_DIAG_EVENT_USB_TX_WAIT_TIMEOUT` in `config/app/usb_comm/usb_comm_hid.c` when HID TX-ready wait times out
- add `HW75_DIAG_EVENT_USB_TX_WRITE_FAIL` when `hid_int_ep_write()` fails
- add `HW75_DIAG_EVENT_USB_TX_SHORT_WRITE` when the endpoint reports a short write
- temporary firmware marker: `+usbtxdiag1`

This is intended to prove or eliminate the modified USB send path as the actual failure boundary after `RGB_CONTROL`.

## 2026-04-05 Runtime Stack Watermark Diagnostic

`+usbtxdiag1` still did not surface any explicit USB TX failure event before the freeze.

Current staged diagnostic now verifies runtime stack pressure directly:

- enable `CONFIG_THREAD_STACK_INFO=y`
- enable `CONFIG_INIT_STACKS=y`
- log only new minimum free-stack watermarks
- `HW75_DIAG_EVENT_USB_STACK_WATERMARK` from `config/app/usb_comm/usb_comm_proto.c`
- `HW75_DIAG_EVENT_RGB_WORKQ_STACK_WATERMARK` from `deps/zmk/app/src/rgb_underglow.c`
- temporary firmware marker: `+stackdiag1`

This diagnostic is intended to prove or eliminate stack/resource exhaustion as the shared root cause between log transport and active RGB work.

Latest local evidence after getting `+stackdiag1` to build:

- the build now reports `SRAM 20272 B / 20 KB = 98.98%`, so static headroom is only about `208 B`
- `About` / version fetch only sends `VERSION`; it does not itself send `LOG_*`
- `deps/zmkx.app/src/routes/Debug.vue` is what starts/stops debug polling, so traces full of `LOG_GET_EVENTS` still mean Debug polling is active
- `config/app/diag_log.c` ring updates are protected by `k_spinlock`, so there is no obvious unlocked ring corruption in the added log store
- large SRAM users in the current build include `ws2812_spi_0_px_buf` (`2472 B`), `diag_state` (`724 B`), `usb_comm_thread_stack` (`1024 B`), `sys_work_q_stack` (`1024 B`), and `lowprio_q_stack` (`768 B`)

## 2026-04-05 Duplicated LOG_GET_EVENTS Buffer Removal

The user clarified that the earlier binary-log migration build they remembered was around `93%` SRAM, and the later increase happened after additional log-system work.

Local root-cause tracing then found one concrete duplicated buffer in the new log path:

- `handler_debug_log.c` already keeps fetched event batches in `static struct log_encode_context ctx.events[4]`
- but `LogEvents.events` was also generated as a static repeated array inside every `usb_comm_MessageD2H`
- because `usb_comm_handle_message()` allocates `usb_comm_MessageD2H d2h` on the USB comm thread stack, even a `VERSION` response was paying for that extra 4-event array

Measured generated sizes before removal:

- `usb_comm_LogEvents = 188 B`
- `usb_comm_MessageD2H = 192 B`

Current local fix:

- switch `LogEvents.events` to callback encoding
- keep only `ctx.events[]` as the single event-batch buffer
- new temporary version marker: `+stackcb1`

Measured generated sizes after removal:

- `usb_comm_LogEvents = 32 B`
- `usb_comm_MessageD2H = 68 B`

This is a runtime stack-usage fix. It does not lower the reported SRAM percentage by itself because the linker memory report counts reserved thread stacks/BSS, not actual stack peak usage inside the existing `usb_comm_thread_stack`.

## 2026-04-05 Diagnostic Rollback After Root-Cause Confirmation

Hardware verification with the `+stackcb2` marker showed:

- RGB and normal binary log transport are healthy again
- the duplicated static `LOG_GET_EVENTS` event array was a real root-cause contributor
- stack low-water still reached `32 B` free at `LOG_GET_EVENTS` stage 3, but the board no longer hard-locked

After that confirmation, temporary diagnostics were rolled back from the default build:

- removed `LOG_GET_EVENTS` self-decode verification / sentinel replacement from `config/app/usb_comm/usb_comm_proto.c`
- removed failure-only USB TX diagnostic event emission from `config/app/usb_comm/usb_comm_hid.c`
- removed the temporary version suffix from `config/app/usb_comm/handler/handler_version.c`

Stack watermark instrumentation remains in source but is no longer enabled by default:

- `CONFIG_THREAD_STACK_INFO` disabled again
- `CONFIG_INIT_STACKS` disabled again
- this avoids the additional SRAM/runtime overhead in normal firmware builds

Current clean-build memory after rolling back the diagnostics:

- `FLASH: 71572 B / 104 KB = 67.21%`
- `SRAM: 20248 B / 20 KB = 98.87%`

## 2026-04-05 Boot Events Restored First

As planned, `boot_events` were restored before `snapshots`.

Local changes:

- `handler_debug_log.c`
  - keep `ctx.snapshot_count = 0U`
  - stop forcing `ctx.boot_event_count = 0U`
- `Debug.vue`
  - update Boot Events empty-state copy to "No boot events captured yet."

Build status:

- firmware build succeeded
- host build succeeded

Memory after the USB buffer reduction plus boot-event restore:

- `FLASH: 71568 B / 104 KB = 67.20%`
- `SRAM: 19672 B / 20 KB = 96.05%`

Snapshots are still intentionally disabled at this point. The next hardware question is whether Boot Events now renders the expected startup entries while the transport baseline remains healthy.

## 2026-04-05 Boot Events Encode Regression

The first boot-event restore attempt did not break the keyboard, but it did break `LOG_GET_STATE` decoding on host:

- `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR` still decoded
- `LOG_GET_STATE` became a deterministic malformed multi-packet response
- host trace repeatedly showed `invalid wire type 4 at offset 53`
- raw queue bytes showed the corruption was inside the `boot_events` section of `LOG_GET_STATE`

Local conclusion:

- this matched the earlier `LOG_GET_EVENTS` issue class
- `boot_events` callback was still constructing each `usb_comm_LogEvent` as a stack-local temporary during encode
- `LOG_GET_EVENTS` had already been fixed by pre-filling protobuf structs and encoding them directly

Current local fix:

- convert `ctx.boot_events[]` to `usb_comm_LogEvent`
- fill boot events with `fill_log_event()`
- encode boot events directly with `pb_encode_submessage(..., &ctx.boot_events[i])`
- temporary version marker: `+bootcb1`

Build status:

- firmware build succeeded
- host build was not required

Current memory after this fix:

- `FLASH: 71600 B / 104 KB = 67.23%`
- `SRAM: 19736 B / 20 KB = 96.37%`

## 2026-04-05 Boot Events Follow-Up: Empty Snapshots Callback Suspect

`+bootcb1` did not change the malformed `LOG_GET_STATE` bytes. The board stayed alive and non-debug requests still decoded, so this remained a narrow `LOG_GET_STATE` encode-path issue.

What was verified:

- the malformed bytes were deterministic across retries
- the bad region was not aligned with the HID packet boundary, so this was not a host packet reassembly bug
- `LOG_GET_EVENTS` remained healthy with a single populated callback field

Current working hypothesis:

- `LOG_GET_STATE` still had an empty `snapshots` callback attached even though snapshots were intentionally disabled
- `boot_events` then followed as a populated callback field
- this empty-then-populated callback sequence is now the most specific remaining difference from the healthy `LOG_GET_EVENTS` path

Current local change:

- only attach the `snapshots` callback when `ctx.snapshot_count > 0`
- only attach the `boot_events` callback when `ctx.boot_event_count > 0`
- remove the unfinished self-decode helper remnants from `usb_comm_proto.c`
- temporary version marker: `+bootnosnapcb1`

Build status:

- firmware build succeeded
- host build was not needed

Current memory:

- `FLASH: 71488 B / 104 KB = 67.13%`
- `SRAM: 19736 B / 20 KB = 96.37%`

## 2026-04-05 LOG_GET_STATE Source Probe

`+bootnosnapcb1` still produced the same malformed `LOG_GET_STATE` bytes, so host raw traces alone were no longer sufficient to place the first corruption point.

Current diagnostic enhancement is intentionally source-oriented:

- encode `boot_event[0]` standalone inside `handler_debug_log.c` and log its first 16 encoded bytes
- log `LOG_GET_STATE` meta (`oldest_seq`, `newest_seq`, boot-event count, snapshot count)
- after full `LOG_GET_STATE` encode in `usb_comm_proto.c`, log response bytes `[11..18]` and `[19..26]`
- decode these temporary probe events in host `debug_decode.ts`
- temporary firmware marker: `+stateprobe1`

Interpretation target:

- standalone event bad => source event/protobuf conversion bug
- standalone event good but final response bad => `LogState` composition/encode bug
- final firmware-side response bytes already equal host malformed bytes => transport is not the corruption source

## 2026-04-05 Host Probe Fetch Fix

The first `+stateprobe1` trace proved another boundary:

- `LOG_GET_STATE` still malformed
- host never showed events 112-116
- root cause on host side was control flow, not missing firmware data:
  - `startPolling()` performs `LOG_GET_STATE` first
  - on startup failure it stopped immediately
  - so it never fetched `LOG_GET_EVENTS`, even though the firmware probe events had already been logged

Current local host fix:

- after startup `LOG_GET_STATE` failure, do one best-effort `LOG_GET_EVENTS` fetch before fully unwinding
- keep the original startup error visible
- this allows the state-probe evidence to be collected without changing firmware behavior again

## 2026-04-05 Root Cause Narrowed To TX Packet Buffer Reuse

The first `+stateprobe1` probe event finally put the first corruption point after protobuf encode:

- firmware-side `LOG_GET_STATE response bytes[11..18]` were valid
- host still received malformed bytes for the same multi-packet response
- only multi-packet responses were affected

That makes the shared multi-packet TX buffer the strongest root cause.

Specific bug:

- `usb_comm_hid_send()` used a single static `tx_buf`
- it filled packet N
- called `hid_int_ep_write()`
- then immediately filled packet N+1 into the same `tx_buf`
- only after rewriting the buffer did it wait for the IN endpoint semaphore

If the HID driver consumes caller memory asynchronously, packet N can be corrupted by packet N+1 before transmission finishes.

Current local fix:

- wait for `hid_sem` first
- only then fill the shared `tx_buf`
- temporary version marker: `+txbuffix1`

## 2026-04-05 TX Buffer Root Cause Confirmed And Probe Cleaned Up

Hardware confirmed the fix:

- `LOG_GET_STATE` decoded successfully again
- Boot Events rendered normally
- the transport stayed healthy

This closes the root-cause investigation:

- protobuf composition was not the corruption source
- host packet reassembly was not the corruption source
- the fault was shared multi-packet TX buffer reuse in `usb_comm_hid_send()`

Cleanup after confirmation:

- remove temporary probe events 112-116
- remove the temporary host-side best-effort probe fetch
- remove the temporary version suffix
- keep only the proven send-order fix in `usb_comm_hid.c`

Current plan state:

- `boot_events` restored and healthy
- `snapshots` have now been restored locally and need hardware verification

Latest local change:

- removed the temporary `ctx.snapshot_count = 0U` clamp from
  `config/app/usb_comm/handler/handler_debug_log.c`
- `LOG_GET_STATE` now includes both `snapshots` and `boot_events` again
- updated the Debug page empty-state copy accordingly

Latest hardware verification:

- `snapshots` restore is healthy on real hardware
- `LOG_GET_STATE`, `LOG_GET_EVENTS`, Boot Events, and RGB all remain healthy
- transport remains stable while polling
- observed snapshots currently include `System`, `HID`, and `Indicator`

State of the migration after this verification:

- binary debug-log transport is now stable enough to leave transport debugging
- TouchBar-specific investigation can resume next

Latest TouchBar root-cause finding:

- the startup failure is not yet a TouchBar behavior bug
- Boot Events ordering shows `touchbar_init()` is running before the chosen `kscan` device
  finishes init
- both were using `APPLICATION / CONFIG_APPLICATION_INIT_PRIORITY`
- local fix moves `touchbar_init()` to `CONFIG_APPLICATION_INIT_PRIORITY + 1`

Latest follow-up:

- hardware confirmed the init-order fix:
  - `TOUCHBAR_INIT_FAIL` disappeared
  - `TOUCHBAR_INIT` now appears
  - TouchBar snapshot shows `phase Init`
- host-side `KSCAN_TOUCH_MAP` text was still misleading because it decoded nibble-packed
  row/column fields as bytes
- local host fix now decodes those fields as 4-bit nibbles

Latest hardware detail:

- current decoded TouchBar map is:
  - `0=r10/c1`
  - `1=r10/c3`
  - `2=r10/c5`
  - `3=r10/c0`
  - `4=r10/c2`
  - `5=r10/c4`

This matches the transformed key positions and means the next debugging stage is live touch
state/gesture tracing rather than init-ordering.

Latest live-trace result:

- TouchBar raw/debounced/logical touch transitions are healthy
- Pan-mode gesture state machine is healthy
- HID wheel emission is healthy

Remaining issue:

- Debug header `Dropped` count can still increase during TouchBar interaction
- this is device ring overwrite, not transport corruption
- root cause is the host still using the diagnosis-time `MAX_BATCH = 1` fetch throttle while
  firmware ring size is only 12

Latest local fix:

- restore host `LOG_GET_EVENTS` batch size from `1` to `4`

Latest interpretation of the live trace:

- Pan mode is now verified end-to-end
- remaining confusing values like `254/252/250` were host decode issues, not firmware behavior
- host now decodes wheel/step directions as signed 8-bit values

Next stage:

- validate `tb_mode`
- then App Switch and Desktop Switch behaviors

## 2026-04-05 App/Desktop Edge-Step Root Cause

The next TouchBar bug turned out not to be in transport, init order, or the overlapping
left/right segment ownership itself.

What full trace review showed:

- segment ownership is already sticky per gesture and matches the intended overlap model:
  - left segment: `0/1/2/3`
  - right segment: `2/3/4/5`
- the visible misbehavior appears after that ownership is already fixed
- App/Desktop were still deriving discrete steps from one fixed gesture anchor using direct
  `displacement / step_distance`
- with real edge traces like `0x20 -> 0x30 -> 0x10`, that means adjacent-pad redistribution can
  look like intentional inward motion even while the finger still feels parked at the edge
- the same fixed-anchor quantization also explains mid-gesture oscillation like `+1 -> -1` when
  contact jitters around one threshold

So the root cause is:

- step quantization in App/Desktop was using the wrong reference model for noisy edge contact
- not a bad `kscan` map
- not a bad half selection
- not a bad host decode

Current local fix in `config/app/touchbar.c`:

- split the old single anchor into:
  - `anchor_position` for gesture-origin semantics still needed by Pan and desktop pre-seek finalize
  - `step_anchor_position` for App/Desktop step quantization
- ratchet `step_anchor_position` after every normal App/Desktop step
  - this removes the old fixed-anchor reversal path
- keep repeated edge stepping by moving `step_anchor_position` outward only for edge-repeat steps
- add `step_entry_edge_direction`
  - if a gesture starts at the far left/right edge, the first inward step is held back until the
    contact has clearly left that edge zone

Why this is evidence-based:

- the original migrated constants and segment maps already matched the old firmware
- the failure only appeared in traces where the owned segment stayed the same but the computed
  position redistributed between adjacent pads
- that isolates the bug to step interpretation, not touch ownership

Build status after the fix:

- firmware build succeeded
- host build was not required

Current memory after rebuild:

- `FLASH: 71732 B / 104 KB = 67.36%`
- `SRAM: 19744 B / 20 KB = 96.41%`

Next verification focus:

1. App Switch edge hold should no longer emit the first opposite-direction step too early.
2. App Switch mid-gesture should no longer chatter across one threshold.
3. Desktop Switch edge hold should continue in the held edge direction more reliably.

## 2026-04-05 TouchBar Mapping Mismatch Found

The next retest showed that even with the step-model fix, the physical behavior still did not
match the intended left/right ownership:

- left-side touches produced logs but often did not bring up the App Switch UI
- the UI would often appear only after the finger moved much farther right
- dragging back left partially worked, which is consistent with wrong logical channel ordering
  rather than purely wrong gesture thresholds

Root cause verified from old firmware:

- original `HelloWord/hw_keyboard.cpp` explicitly maps TouchBar logical positions with:
  - `RAW_BIT_BY_LOGICAL_POSITION = {0, 5, 4, 3, 2, 1}`
- the migrated ZMK driver had been deriving logical order from `matrix-transform` positions
  `82..87`
- on this board revision those positions are ordered:
  - `RC(10,1) RC(10,3) RC(10,5) RC(10,0) RC(10,2) RC(10,4)`
- so the migrated map was:
  - `0=r10/c1 1=r10/c3 2=r10/c5 3=r10/c0 4=r10/c2 5=r10/c4`
- that order does not match the original firmware’s logical left-to-right order

Current local fix in `config/drivers/kscan/kscan_gpio_74hc165.c`:

- stop using the `matrix-transform` order for TouchBar logical export
- explicitly export TouchBar channels in the original firmware order:
  - row `10`
  - cols `0, 5, 4, 3, 2, 1`

Expected startup log after flashing:

- `TouchBar logical map is 0=r10/c0 1=r10/c5 2=r10/c4 3=r10/c3 4=r10/c2 5=r10/c1.`

Why this is the correct next fix:

- it restores the old firmware’s proven logical ordering exactly
- it explains the user’s physical observation better than further step-threshold tuning does

Build status:

- firmware build succeeded
- host build not required

## 2026-04-05 TouchBar Mapping Re-verified On Hardware

The previous "match old firmware exactly" mapping conclusion turned out to be incomplete.

What was done to verify it:

- a temporary firmware diagnostic mapped TouchBar logical positions `0..5` onto the
  key-backlight LEDs under number-row keys `1..6`
- the user then touched each physical TouchBar pad from left to right and reported the observed
  LED mapping:
  - `1 -> 3`
  - `2 -> 6`
  - `3 -> 4`
  - `4 -> 2`
  - `5 -> 1`
  - `6 -> 5`

What that proves:

- the original interim hard-coded export order `0, 5, 4, 3, 2, 1` was still wrong for the
  migrated board wiring / scan interpretation
- the correct physical left-to-right order for row `10` is:
  - cols `4, 1, 3, 5, 0, 2`

Current real fix in `config/drivers/kscan/kscan_gpio_74hc165.c`:

- TouchBar logical export order is now hard-coded as:
  - rows: `10 10 10 10 10 10`
  - cols: `4, 1, 3, 5, 0, 2`

Result after flashing and retesting:

- user reported TouchBar operation became "顺手多了"
- timing / delay behavior also felt correct

Cleanup already completed:

- the temporary backlight diagnostic overlay was removed again
- only the verified TouchBar source mapping fix remains

Current build status after cleanup:

- firmware build succeeded
- temporary diagnostic code is gone
- memory: `FLASH 71732 B / 104 KB = 67.36%`, `SRAM 19744 B / 20 KB = 96.41%`

## 2026-04-09 USB Stack Root Cause Re-verified

The later "RGB + Debug still crashes" report was traced again from runtime evidence instead of
guessing at stack sizes.

What was re-enabled:

- `CONFIG_THREAD_STACK_INFO=y`
- `CONFIG_INIT_STACKS=y`
- USB comm low-water logging in `config/app/usb_comm/usb_comm_proto.c`
- RGB workqueue low-water logging in `config/app/rgb_effects.c`

One source-level issue was exposed immediately after turning the diagnostic back on:

- `usb_comm_log_stack_watermark()` in
  `config/app/usb_comm/usb_comm_proto.c` had never actually been compiled before
- once the conditional build path became active, it failed because `diag_log.h` had not been
  included there
- that include has now been added, so stack instrumentation is real, not dead code

The first decisive runtime evidence from hardware was:

- `USB comm stack crossed the 50/70/90/100% used watermark at stage 1 while handling VERSION`
- all four records reported `0 B free of 1024 B`

Why that mattered:

- `VERSION` is one of the lightest requests
- stage `1` is logged before the action handler runs
- so the overflow was not caused by `VERSION` logic, RGB logic, or any specific handler
- it had to come from the common `usb_comm_handle_message()` frame itself

That root-cause tracing was then verified by measuring the protobuf struct sizes actually used in
that frame:

- `usb_comm_MessageH2D = 312 B`
- `usb_comm_MessageD2H = 300 B`
- `pb_istream_t = 16 B`
- `pb_ostream_t = 20 B`

Previous self-inflicted bug in `config/app/usb_comm/usb_comm_proto.c`:

- `usb_comm_handle_message()` was allocating both `usb_comm_MessageH2D h2d` and
  `usb_comm_MessageD2H d2h` as stack locals on the `usb_comm` worker thread
- that means even `VERSION` paid for the full largest-union protobuf payloads on a
  `1024 B` stack before any handler-specific work happened

Current real fix:

- move those two protobuf message objects out of the thread stack and into single-thread reused
  static scratch storage:
  - `static usb_comm_MessageH2D usb_h2d_msg;`
  - `static usb_comm_MessageD2H usb_d2h_msg;`
- this is safe because `usb_comm` is a single worker thread with serialized request handling
- this is a root-cause fix, not a blind stack-size increase

Important interpretation:

- reported SRAM percentage increased after this fix because those protobuf objects moved from
  runtime thread-stack pressure into `.bss`
- that does **not** mean the fix regressed runtime safety
- it means reserved RAM is now more honest while the dangerous stack peak is lower

Hardware verification after the fix showed:

- USB comm no longer hits `0 B free` at `VERSION / stage 1`
- current observed USB watermark:
  - `LOG_GET_STATE`
  - `stage 3`
  - `296 B free of 1024 B`
  - about `71% used`
- this confirms the original crash root cause was the stack-local protobuf message pair

Current build status after this fix:

- firmware build succeeded
- current build report:
  - `FLASH 73824 B / 104 KB = 69.32%`
  - `SRAM 19912 B / 20 KB = 97.23%`
- current firmware artifact:
  - `build/keyboard12/zephyr/zmk.uf2`

## 2026-04-09 Host Proto Drift And RGB Watermark Decode Fix

While reading the new stack-watermark logs, another root cause was found on the host side.

Verified host/proto drift:

- firmware uses `config/proto/usb_comm.proto`
- host proto generation had been reading a stale copy at:
  - `deps/zmkx.app/proto/comm.proto`
- firmware already defined:
  - `LOG_EVENT_USB_STACK_WATERMARK = 110`
  - `LOG_EVENT_RGB_WORKQ_STACK_WATERMARK = 111`
- the stale host proto stopped at `100`
- result: host rendered real watermark events as generic
  `Binary event 110 data0=... data1=...`

Current host fix:

- `deps/zmkx.app/build-proto.mjs` now generates the host protocol directly from:
  - `../../config/proto/usb_comm.proto`
- a compatibility export alias was added so existing frontend imports can keep using `UsbComm`
  even though the formal protobuf package is `usb.comm`
- `deps/zmkx.app/src/stores/debug_decode.ts` also keeps a numeric fallback for `110/111`
  so future enum drift cannot silently fall back to opaque binary text again

RGB watermark decode also had a second issue:

- RGB watermark events existed in two field-packing variants across local diagnostic builds
- host had only been decoding the newer format
- that produced misleading text such as:
  - `RGB workqueue stack crossed the 0% used watermark at stage 6 while effect 1 was active`

Current host decode fix:

- `deps/zmkx.app/src/stores/debug_decode.ts` now supports both RGB watermark encodings
- old-format RGB events are now rendered as a generic human-readable watermark record instead of
  showing fake `0%` / bogus stage numbers

Current interpreted runtime state after the above fixes:

- USB comm stack pressure was the actual crash source and is now corrected
- RGB workqueue watermarks seen so far are not crash-class:
  - examples observed on hardware were `472 B free / 768 B` and `376 B free / 768 B`
- if future instability returns, start from fresh runtime evidence rather than re-guessing the old
  USB stack-overflow hypothesis that has already been verified and fixed
## 2026-04-10 TouchBar Multi-Segment Generalization And Keyboard Sync Boundary

This pass solved two different UX complaints, and they need to stay separated in future work:

1. TouchBar needed more than two segments and needed its strip preview to render from real masks
2. keyboard config UI looked like it should live-sync to the board, but no such firmware path exists

### TouchBar root cause that was fixed

The old limitation was not just a frontend rendering problem.

The whole stack was binary left/right:

- `config/app/include/app/touchbar.h`
- `config/app/touchbar.c`
- `config/proto/usb_comm.proto`
- `config/app/usb_comm/handler/handler_touchbar.c`
- `deps/zmkx.app/src/routes/Touchbar.vue`

So adding more visual segments on the host alone would have been fake.

### TouchBar current state

TouchBar is now generalized across firmware, protocol, and host:

- firmware config view now supports segment mask arrays up to 6 segments
- runtime TouchBar logic now uses `touchbar_segment_count`
- shared-point counting and segment selection are N-segment aware instead of left/right-only
- protocol now adds:
  - `repeated uint32 segment_touch_masks`
  - `repeated uint32 segment_entry_masks`
- legacy left/right mask fields are still retained for backward compatibility
- host TouchBar page now:
  - supports add/remove segment
  - edits each segment’s touch span and entry span independently
  - renders continuous rounded segment bands directly from the current masks

Current deliberate limit:

- max segment count is `6`
- this matches the 6 logical touch points and current nanopb fixed storage

### Keyboard config live-sync boundary

The keyboard page complaint was valid: it did not actually sync live to the keyboard.

Verified root cause:

- current active firmware target does not expose runtime keymap editing over the USB config channel
- `config/proto/usb_comm.proto` has no keymap action
- current USB handlers do not implement runtime keymap mutation for `hw75_keyboard`
- current host keyboard page edits and regenerates `.keymap` source only

Therefore the correct immediate fix was:

- do not fake a "sync to keyboard" button
- update the keyboard page to explicitly say live keymap sync is not currently supported by firmware
- keep the page positioned as:
  - edit draft
  - save/copy/export `.keymap`

If true runtime keyboard sync is requested later, treat it as a new firmware feature, not a host-only polish task.

### Current builds after this work

- host build passed:
  - `cd deps/zmkx.app && npm run build`
- firmware build passed:
  - `python -m west build -d build/keyboard12`
- current firmware artifact:
  - `build/keyboard12/zephyr/zmk.uf2`
- latest build report:
  - `FLASH 76088 B / 104 KB = 71.45%`
  - `SRAM 20168 B / 20 KB = 98.48%`

### Important warning for next work

- TouchBar multi-segment support is now real end-to-end
- keyboard live-sync is still missing at the firmware/protocol layer
- SRAM headroom is now extremely tight, so any future runtime keymap feature must be designed with RAM cost first, not added casually
