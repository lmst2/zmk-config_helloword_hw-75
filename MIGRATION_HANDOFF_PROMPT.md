Continue work in `e:\code\zmk-config_helloword_hw-75`.

Read first:

- `e:\code\zmk-config_helloword_hw-75\MIGRATION_HANDOFF_NEXT.md`
- `e:\code\zmk-config_helloword_hw-75\MIGRATION_HANDOFF.md`
- if needed, `e:\code\zmk-config_helloword_hw-75\MIGRATION_CONTEXT.md`

Important constraints:

1. Do not redesign the project at a high level.
2. Do not switch to TouchBar behavior debugging yet.
3. The immediate task is to stabilize and verify the binary debug log transport on real hardware.
4. Do not restore `snapshots` or `boot_events` before first confirming whether `LOG_GET_EVENTS` is healthy again.
5. The repo already vendors local ZMK in `deps/zmk` and host app in `deps/zmkx.app`.
6. Continue from the current implementation and current diffs.
7. When debugging transport, do not stop at a plausible symptom and patch around it.
8. First trace the exact data flow and byte flow end-to-end, verify where corruption first appears, and only then change code.
9. Treat unverified explanations as hypotheses, not conclusions.
10. Prefer root-cause fixes over workaround patches or memory-expensive buffering.
11. When deciding what to suspect first, suspect code we changed before suspecting untouched code.
12. Trust the compiler/toolchain by default; only investigate compiler or nanopb bugs after normal code/data-flow causes have been exhausted.
13. If a workaround is added for diagnosis, mark it as temporary and remove or revisit it once it has served its diagnostic purpose.

Current true state:

- keyboard typing works again
- keyboard RGB works again
- host app dynamic-only menus issue was already fixed
- binary debug logging system exists end-to-end
- `LOG_GET_STATE` is intentionally compact right now
- `snapshots` and `boot_events` are still temporarily disabled in state response
- host trace UI is now collapsed by default and repeated trace entries are coalesced
- host transport trace is now retained across disconnect until manually cleared
- multi-event batch transport was re-enabled to 4 earlier, and a transport regression appeared on real hardware after that
- firmware-side self-decode verification for `LOG_GET_EVENTS` was added in `usb_comm_proto.c`
- temporary flashed-build marker exists in `handler_version.c`
- current expected `VERSION.app_version` suffix is `+usbtxwait1`
- previous suffix `+usbsharedoff1` belonged to the earlier diagnostic build where shared usb_comm-layer diag logging had been removed, but the likely-corrupt first HID startup event was still present
- latest real-hardware result confirmed diagnostic firmware can run on-device, but opening Debug still hard-freezes the keyboard
- the newest observed failure boundary is earlier than before: `VERSION` still decodes, but `LOG_GET_STATE` may receive no response at all
- `usb_comm` thread creation in `usb_comm_proto.c` was temporarily changed to `K_PRIO_PREEMPT(...)` during diagnostics, and has now been changed back to `K_PRIO_COOP(...)` because it was a plausible startup-active confounder
- after that `K_PRIO_COOP(...)` rollback, the user observed that UF2 copy recovered but opening the host app could freeze the keyboard before `VERSION` visibly completed
- request-path HID send logic in `usb_comm_hid.c` has now been changed back away from active `hid_int_ep_write(...)/-EAGAIN` retry to `int_in_ready` callback gating for the next retest
- the newer `+usbtxready1` build was then observed on hardware and `VERSION` briefly displayed correctly
- after that, trying to change RGB lighting effect froze the keyboard
- after disconnect/reconnect following that freeze, `VERSION` was no longer readable again
- code search confirmed RGB page does not send `LOG_*` host requests
- however, firmware-side generic `USB_REQUEST` logging was still running for normal requests like `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR`
- that generic request logging has now been temporarily disabled for the next isolation build
- firmware rebuild after that change succeeded, producing a new UF2 marked `+usbtxrqoff1`
- latest host trace still showed zero RX packets even for `VERSION`
- shared diag-log writes from `usb_comm_proto.c` and `usb_comm_hid.c` have now also been temporarily removed for the next stronger isolation build
- latest trace on `+usbsharedoff1` restored RX and decode for `VERSION`, `LOG_GET_STATE`, and a recovered `LOG_GET_EVENTS` batch
- remaining corruption appears concentrated in the first startup event only
- the `HW75_DIAG_EVENT_HID_INIT` startup event is now temporarily disabled for the next narrow isolation build
- latest traces on `+hidhinitoff1` show `VERSION` and `LOG_GET_STATE` decoding, but `LOG_GET_EVENTS` is still the next failing stage
- host-side `LOG_GET_EVENTS` polling has now been temporarily reduced to `maxCount=1` for the next boundary retest
- code-path tracing after the "keyboard freezes after opening host app" report found a lower shared-transport root-cause candidate:
- `usb_comm_thread` runs as `K_PRIO_COOP(...)`
- `usb_comm_hid_send()` currently ignores `k_sem_take(..., K_MSEC(30))` timeout and still writes the HID IN endpoint
- Zephyr `usb_write()` retries `-EAGAIN` using `k_yield()`; in a cooperative thread this can monopolize CPU and make the keyboard appear dead
- fix and verify that send-timeout path before chasing more surface-level `LOG_*` or RGB request symptoms
- after the latest `+usbtxwait1` retest, trace-and-code review found a deeper host/firmware mismatch:
- firmware request intake is single-slot (`usb_rx_buf` / `usb_rx_len` / `usb_rx_idx` plus `k_sem` max count `1`)
- host currently overlaps requests from `VERSION`, RGB auto-fetch, and Debug polling without a global request-response lock
- treat that host-side overlapping as the strongest root cause now
- serialize host requests end-to-end before adding more firmware buffering or changing protocol shape again

Latest key files changed:

- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\usb_comm_proto.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\usb_comm_hid.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\handler\handler_version.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\handler\handler_debug_log.c`
- `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\src\stores\debug.ts`
- `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\src\stores\usb.ts`
- `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\src\utils\usb\usb.ts`
- `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\src\utils\usb\usb-hid.ts`
- `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\src\routes\Debug.vue`

Latest build outputs:

- firmware UF2: `e:\code\zmk-config_helloword_hw-75\build\keyboard12\zephyr\zmk.uf2`
- host dist: `e:\code\zmk-config_helloword_hw-75\deps\zmkx.app\dist\index.html`

Latest memory status after the newest fix:

- `FLASH: 72768 B / 104 KB = 68.33%`
- `SRAM: 20168 B / 20 KB = 98.48%`

Do not start memory optimization yet unless the transport is confirmed working and SRAM blocks the next step.

Immediate next step:

1. Flash the latest UF2.
2. Confirm the running firmware reports `VERSION.app_version` with suffix `+usbtxwait1`.
3. Run the latest host app.
4. First verify whether `VERSION` gets any decodable RX response at all.
5. If `VERSION` is healthy, next distinguish whether the first RGB-side failing request is `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`.
6. Only after that, open Debug page.
7. Expand `Transport Trace`.
8. Verify whether `LOG_GET_STATE` receives any RX response at all after `VERSION`.
9. Only if `LOG_GET_STATE` is healthy again, continue checking whether `LOG_GET_EVENTS` decodes and the `Events` panel fills.

If events are still broken:

- stay focused on the log transport only
- first determine whether the earliest failure is now before `LOG_GET_STATE`, before `VERSION`, or newly on the RGB request path after a healthy `VERSION`
- inspect trace for whether `VERSION` gets zero RX packets, malformed RX data, or a timeout
- inspect trace for whether the first failing RGB request is `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`
- inspect trace for whether `LOG_GET_STATE` gets zero RX packets, malformed RX data, or a timeout after partial RX
- do not pivot to TouchBar logic

If events are working again:

- decide whether to restore `boot_events` or `snapshots` first
- prefer restoring `boot_events` first

When reporting back, include:

- which files you changed
- why
- whether host build ran
- whether firmware build ran
- whether `+usbtxwait1` was observed on hardware
- whether `VERSION` got zero RX packets, malformed RX data, or a timeout
- whether RGB freeze first appeared on `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`
- whether the temporary version marker is still needed
- current risks

## Newer RGB-Side Evidence

Later hardware testing proved binary log transport itself can be healthy after the host-side global request serialization fix.

However, the user then found that those successful log runs happened only while RGB was not visibly lit. This matches `CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=y`.

After idle sleep and a wake keypress, RGB becomes visibly active again. If the host app is started while RGB is already on, the lighting freezes immediately and the keyboard hard-locks.

So the current earliest proven trigger is:

- active RGB refresh + host USB traffic

Temporary local diagnostic already staged:

- remove `irq_lock()/irq_unlock()` around `led_strip_update_rgb()` in `deps/zmk/app/src/rgb_underglow.c`
- temporary version marker: `+rgbirqoff1`

Next window should verify explicitly:

1. whether `VERSION.app_version` shows `+rgbirqoff1`
2. whether host startup while RGB is idle-dark still remains healthy
3. whether host startup while RGB is visibly on still hard-locks the board
4. whether removing the interrupt-off LED refresh path eliminates the freeze

Latest local correction:

- `+rgbirqoff1` did not resolve the freeze and that diagnostic has been backed out
- current staged diagnostic instead removes only the RGB diagnostic log/snapshot writes from `config/app/usb_comm/handler/handler_rgb.c`
- new temporary firmware marker: `+rgblogoff1`

Next verification should prefer this question first:

1. whether `VERSION.app_version` shows `+rgblogoff1`
2. whether `RGB_CONTROL` still freezes with RGB handler diagnostic writes removed
3. whether active-RGB startup still freezes even before any RGB mutation request

Latest local follow-up:

- `+rgblogoff1` still did not change the freeze
- current staged diagnostic now adds failure-only USB send telemetry in `config/app/usb_comm/usb_comm_hid.c`
- new temporary firmware marker: `+usbtxdiag1`

Next window should report explicitly:

1. whether `VERSION.app_version` shows `+usbtxdiag1`
2. whether any new `USB_TX_WAIT_TIMEOUT`, `USB_TX_WRITE_FAIL`, or `USB_TX_SHORT_WRITE` events appear before/after the RGB-triggered freeze
3. whether the last successful host RX was still before `RGB_CONTROL` response

Latest local follow-up:

- `+usbtxdiag1` still did not produce any explicit USB TX failure event before the freeze
- current staged diagnostic now measures runtime stack low-water marks directly
- new temporary firmware marker: `+stackdiag1`
- `+stackdiag1` now builds locally and reports `SRAM 20272 B / 20 KB = 98.98%` (about `208 B` static headroom)
- root-cause tracing then found a duplicated static `LOG_GET_EVENTS` event array inside `usb_comm_MessageD2H`
- current local fix removes that duplicate and encodes from the existing `ctx.events[]` buffer via callback
- measured generated sizes changed from `usb_comm_LogEvents = 188 B` / `usb_comm_MessageD2H = 192 B` to `usb_comm_LogEvents = 32 B` / `usb_comm_MessageD2H = 68 B`
- new temporary firmware marker after that fix: `+stackcb1`

Next window should report explicitly:

1. whether `VERSION.app_version` shows `+stackcb1`
2. whether `USB comm stack watermark dropped to ...` appears, and at which stage/action
3. whether `RGB workqueue stack watermark dropped to ...` appears once RGB tick starts
4. the exact minimum free-stack values reported for USB comm thread and RGB workqueue thread

Latest local cleanup after confirmation:

- hardware confirmed the duplicate `LOG_GET_EVENTS` buffer fix restored normal RGB/log behavior
- temporary `LOG_GET_EVENTS` self-decode verification has been removed again
- temporary USB TX failure-only diagnostic events have been removed again
- temporary version suffix has been removed again
- stack watermark instrumentation remains in source but `CONFIG_THREAD_STACK_INFO` / `CONFIG_INIT_STACKS` are disabled again by default to avoid extra overhead
- current clean-build memory is `FLASH 71572 B / 104 KB = 67.21%`, `SRAM 20248 B / 20 KB = 98.87%`

Latest local progress:

- USB buffer limits on `hw75_keyboard` were reduced from `TX 768 / RX 128` to `TX 256 / RX 64`
- that lowered memory to `FLASH 71572 B / 104 KB = 67.21%`, `SRAM 19672 B / 20 KB = 96.05%`
- `boot_events` have now been restored in `LOG_GET_STATE`
- `snapshots` remain disabled
- firmware build succeeded
- host build succeeded

Next window should verify explicitly:

1. whether Boot Events now shows the expected startup entries
2. whether Snapshots still remain empty/disabled
3. whether `VERSION`, `RGB`, `LOG_GET_STATE`, and `LOG_GET_EVENTS` remain healthy after the boot-event restore

Latest local follow-up:

- the first boot-event restore attempt caused deterministic malformed `LOG_GET_STATE` responses on host
- keyboard remained alive, so this was not a full transport regression
- raw queue bytes showed corruption specifically inside the `boot_events` portion of `LOG_GET_STATE`
- current local fix mirrors the earlier `LOG_GET_EVENTS` fix: boot events are now pre-filled as `usb_comm_LogEvent` and encoded directly
- `+bootcb1` did not change the malformed bytes
- the next narrowed hypothesis is the empty `snapshots` callback still being attached ahead of the populated `boot_events` callback in `LOG_GET_STATE`
- `+bootnosnapcb1` still produced the same malformed bytes
- current local diagnostic build now logs:
  - `LOG_GET_STATE` meta (`oldest_seq`, `newest_seq`, boot-event count, snapshot count)
  - standalone encoded bytes for `boot_event[0]`
  - final encoded response bytes `[11..18]` and `[19..26]` for `LOG_GET_STATE`
- these events are emitted through the healthy `LOG_GET_EVENTS` path so the first corruption point can be located without guessing
- new temporary firmware marker: `+stateprobe1`
- host follow-up fix:
  - `startPolling()` now does one best-effort `LOG_GET_EVENTS` fetch even if startup `LOG_GET_STATE` fails
  - this is required because otherwise host aborts before it ever pulls the firmware-side probe events
- first useful probe result:
  - firmware-side `LOG_GET_STATE response bytes[11..18]` were valid before send
  - host still received malformed bytes
  - only multi-packet responses were affected
- current root-cause fix is in `usb_comm_hid_send()`:
  - previously it rewrote the shared static `tx_buf` for packet N+1 before waiting for packet N to complete
  - now it waits on `hid_sem` first, then fills `tx_buf`
- new temporary firmware marker: `+txbuffix1`
- hardware then confirmed this fix
- temporary source-probe diagnostics and the temporary host probe-fetch path have now been removed again
- current code keeps only the actual `usb_comm_hid_send()` ordering fix
- `boot_events` are restored and healthy
- `snapshots` remain disabled and are the next planned restore step

Next window should report explicitly:

1. transport stabilization work for the binary debug-log path is complete
2. `LOG_GET_STATE`, `LOG_GET_EVENTS`, Boot Events, and Snapshots are healthy again
3. first TouchBar root-cause fix is to correct init ordering:
   `touchbar_init()` must run after the chosen `zmk,kscan` device finishes init
4. host decode of `KSCAN_TOUCH_MAP` also needed correction:
   the firmware packs row/col as 4-bit nibbles, not bytes
5. init is now healthy on hardware; continue with live touch-state validation next
6. live touch-state validation is healthy; remaining `Dropped` count is caused by the host still
   fetching only one event per `LOG_GET_EVENTS` while the firmware ring size is 12
7. Pan mode is now verified end-to-end; next debug stage is `tb_mode` cycling plus App/Desktop
   switch gesture validation

Latest local TouchBar behavior finding after reviewing full App/Desktop logs:

- the remaining bug is not segment ownership itself
- ownership is already sticky per gesture and still uses the intended overlapping strips:
  - left: `0/1/2/3`
  - right: `2/3/4/5`
- the bad behavior happens after ownership is chosen
- App/Desktop were still quantizing steps from one fixed gesture anchor using direct
  `displacement / step_distance`
- that lets edge-state redistribution like `0x20 -> 0x30 -> 0x10` look like intentional inward
  movement, which matches the user-observed "still on the edge but it switched the other way"
- it also explains `+1/-1` oscillation when contact jitters around one threshold

Current local fix in `config/app/touchbar.c`:

- keep `anchor_position` for Pan and desktop pre-seek finalize
- add `step_anchor_position` for App/Desktop discrete step quantization
- ratchet `step_anchor_position` after each normal App/Desktop step
- keep edge-repeat behavior by moving `step_anchor_position` outward only for repeated edge steps
- add `step_entry_edge_direction`
  - if a gesture starts on the far left/right edge, the first inward step is suppressed until the
    contact clearly leaves that edge zone

This is intended as a root-cause fix to the step model, not a threshold-only tweak.

Latest local build status after this change:

- firmware build succeeded
- host build was not needed
- memory: `FLASH 71732 B / 104 KB = 67.36%`, `SRAM 19744 B / 20 KB = 96.41%`

Next window should verify explicitly:

1. App Switch far-left/far-right edge hold no longer emits the first opposite-direction step too
   eagerly.
2. App Switch no longer chatters `+1/-1` when hovering near one threshold.
3. Desktop Switch edge hold now continues in the held edge direction more reliably.

Newer TouchBar finding after the next App Switch retest:

- step-model fixes alone were not enough
- the user’s physical report still showed a stronger left/right mismatch:
  - left-side touches logged activity
  - but the App Switch UI often appeared only after sliding much farther right

This led to a deeper root-cause check of TouchBar logical ordering.

Verified mismatch:

- old firmware `HelloWord/hw_keyboard.cpp` uses:
  - `RAW_BIT_BY_LOGICAL_POSITION = {0, 5, 4, 3, 2, 1}`
- migrated ZMK driver was instead exporting TouchBar logical positions from transform slots
  `82..87`
- on board `hw75_keyboard@1.2`, those slots are ordered:
  - `RC(10,1) RC(10,3) RC(10,5) RC(10,0) RC(10,2) RC(10,4)`
- so the migrated runtime map became:
  - `0=r10/c1 1=r10/c3 2=r10/c5 3=r10/c0 4=r10/c2 5=r10/c4`
- that does not match the original firmware’s logical left-to-right order

Current local fix:

- in `config/drivers/kscan/kscan_gpio_74hc165.c`, stop deriving TouchBar logical order from
  `matrix-transform`
- hard-code the board TouchBar export order to match old firmware:
  - row `10`
  - cols `0, 5, 4, 3, 2, 1`

Expected startup diagnostic after flashing:

- `TouchBar logical map is 0=r10/c0 1=r10/c5 2=r10/c4 3=r10/c3 4=r10/c2 5=r10/c1.`

Latest local build status:

- firmware build succeeded
- host build was not needed
- memory unchanged: `FLASH 71732 B / 104 KB = 67.36%`, `SRAM 19744 B / 20 KB = 96.41%`

Next window should verify explicitly:

1. startup `KSCAN_TOUCH_MAP` now matches the old-firmware order above
2. physical far-left touch now maps to logical far-left
3. App Switch UI can be triggered from the intended half without first sliding deep rightward

Latest verified correction after that note:

- the temporary "TouchBar logical channel -> key-backlight LED 1..6" diagnostic proved that the
  interim old-firmware-style order was still wrong
- user-observed physical mapping was:
  - `1 -> 3`
  - `2 -> 6`
  - `3 -> 4`
  - `4 -> 2`
  - `5 -> 1`
  - `6 -> 5`
- the real physical left-to-right source order is therefore:
  - row `10`, cols `4, 1, 3, 5, 0, 2`
- this is now the live mapping in `config/drivers/kscan/kscan_gpio_74hc165.c`
- the temporary LED diagnostic code has already been removed after verification
- user feedback after this fix was that TouchBar operation became much more natural and timing felt
  correct

So any future TouchBar work should start from:

- mapping fix is done and hardware-verified
- temporary mapping diagnostics are already cleaned up
- only pursue further gesture behavior changes if new user feedback shows a remaining issue

## 2026-04-09 Runtime crash root cause that must not be reopened blindly

Later user report:

- opening RGB + Debug together still crashed the board

This was re-traced from runtime evidence, not from guesswork.

What was enabled:

- `CONFIG_THREAD_STACK_INFO=y`
- `CONFIG_INIT_STACKS=y`
- USB comm stack low-water logging in `config/app/usb_comm/usb_comm_proto.c`
- RGB workqueue stack low-water logging in `config/app/rgb_effects.c`

One hidden source issue was immediately exposed:

- `usb_comm_log_stack_watermark()` had never actually compiled before because the stack-info
  config was usually off
- once enabled, it failed because `config/app/usb_comm/usb_comm_proto.c` was missing
  `#include <app/diag_log.h>`
- that include is now fixed

Critical hardware evidence before the fix:

- even `VERSION` hit:
  - `USB comm stack crossed the 50/70/90/100% used watermark at stage 1`
  - with `0 B free of 1024 B`

Why this closed the root-cause search:

- stage `1` happens before the request handler runs
- so the overflow was not in `VERSION`, not in RGB logic, and not in a specific handler
- it had to be in the common `usb_comm_handle_message()` frame

That frame was then measured directly:

- `usb_comm_MessageH2D = 312 B`
- `usb_comm_MessageD2H = 300 B`
- `pb_istream_t = 16 B`
- `pb_ostream_t = 20 B`

Verified root cause:

- `config/app/usb_comm/usb_comm_proto.c`
  - `usb_comm_handle_message()` had stack-local `usb_comm_MessageH2D h2d`
  - and stack-local `usb_comm_MessageD2H d2h`
- on a `1024 B` worker stack, that was enough to overflow before entering even lightweight
  handlers

Verified fix:

- move those two protobuf message objects into reused static scratch storage:
  - `usb_h2d_msg`
  - `usb_d2h_msg`
- do **not** summarize this as "just increased stack" because that was not the fix
- the fix is removing the self-inflicted stack-local protobuf-union allocation pattern

Verified post-fix runtime result:

- USB comm no longer overflows at `VERSION / stage 1`
- latest observed low-water:
  - `LOG_GET_STATE`
  - `stage 3`
  - `296 B free of 1024 B`
  - about `71% used`

Important interpretation:

- SRAM percentage went up because the protobuf scratch moved from runtime stack usage into `.bss`
- that is expected and not a regression in runtime safety

Current firmware build state after this fix:

- build succeeded
- latest report:
  - `FLASH 73824 B / 104 KB = 69.32%`
  - `SRAM 19912 B / 20 KB = 97.23%`
- current artifact:
  - `build/keyboard12/zephyr/zmk.uf2`

If the user later reports a new crash:

- do **not** restart from "maybe the USB stack is too small" as the first theory
- start from fresh post-fix runtime evidence instead

## 2026-04-09 Host protocol / decode drift that was also fixed

While reading the new watermark logs, a second independent issue was verified.

Verified drift:

- firmware protocol source is `config/proto/usb_comm.proto`
- host proto generation had still been using stale `deps/zmkx.app/proto/comm.proto`
- firmware had:
  - `110 = LOG_EVENT_USB_STACK_WATERMARK`
  - `111 = LOG_EVENT_RGB_WORKQ_STACK_WATERMARK`
- stale host bindings stopped at `100`
- result: valid watermark events appeared as generic binary records

Current host fix:

- `deps/zmkx.app/build-proto.mjs` now generates bindings from:
  - `../../config/proto/usb_comm.proto`
- compatibility export alias keeps existing imports using `UsbComm`
- `deps/zmkx.app/src/stores/debug_decode.ts` also keeps a numeric fallback for `110/111`

RGB decode also had a formatting-drift issue:

- two RGB watermark packing variants existed across local diagnostic builds
- host had only decoded the newer one
- that caused misleading text like:
  - `0% used watermark at stage 6`

Current decode fix:

- `debug_decode.ts` now supports both RGB watermark formats
- old-format RGB events are rendered as readable messages instead of fake `0%` / bogus stage data

Current practical meaning:

- USB crash root cause is fixed in firmware
- host protocol/decode drift for watermark events is fixed
- current RGB watermark reports are not crash-class evidence
## 2026-04-10 latest TouchBar / keyboard-config state

Two separate issues were addressed and should stay separate:

1. TouchBar needed more than two segments and needed live mask-based rendering
2. keyboard config page looked like it should sync live to the board, but it cannot yet

### TouchBar

This is now implemented end-to-end.

Root cause that was fixed:

- TouchBar was hard-coded to a binary left/right model across:
  - `config/app/include/app/touchbar.h`
  - `config/app/touchbar.c`
  - `config/proto/usb_comm.proto`
  - `config/app/usb_comm/handler/handler_touchbar.c`
  - `deps/zmkx.app/src/routes/Touchbar.vue`

Current state:

- firmware now supports variable segment count up to 6
- protocol now carries repeated segment masks
- legacy left/right mask fields are still retained for compatibility
- host TouchBar page supports add/remove segment and renders continuous rounded bands from the
  real masks

### Keyboard config

Do **not** describe the keyboard page as live-syncing to the keyboard.

Verified root cause:

- there is no runtime keymap USB action in `config/proto/usb_comm.proto`
- there is no current firmware runtime keymap handler for the active `hw75_keyboard` target
- keyboard page edits/regenerates `.keymap` source only

Current correct UX:

- edit draft bindings/layers/macros
- save/copy/export `.keymap`
- explicitly warn that live keymap sync is not currently supported by firmware

### Current builds

- host build passed
- firmware build passed
- current firmware artifact:
  - `build/keyboard12/zephyr/zmk.uf2`
- latest memory:
  - `FLASH 76088 B / 104 KB = 71.45%`
  - `SRAM 20168 B / 20 KB = 98.48%`

If true runtime keyboard sync is requested later:

- treat it as a new firmware/protocol feature
- do not try to fake it from the host
- keep RAM pressure in mind because current SRAM is already ~98.5%
