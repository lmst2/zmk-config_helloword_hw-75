# HW-75 Debug Transport Handoff

## Read Order

Read this file first.

If more background is needed, then read:

- `MIGRATION_HANDOFF.md`
- `MIGRATION_CONTEXT.md`

## Current Mission

Stay focused on stabilizing and proving the binary debug log transport on real hardware.

Do not pivot to TouchBar behavior debugging yet.
Do not redesign the project at a high level.
Do not restore `snapshots` or `boot_events` before `LOG_GET_EVENTS` is proven healthy.

## Current Confirmed State

- keyboard typing works
- keyboard RGB works
- host app dynamic-only menu issue was already fixed
- binary debug logging exists end-to-end
- `LOG_GET_STATE` is intentionally compact
- `snapshots` and `boot_events` are still disabled in state response
- host trace UI is collapsed by default and repeated trace lines are coalesced
- host transport trace is now retained across disconnects until manually cleared
- event list now sometimes fills with old boot/runtime events, so the chain is not fully dead
- transport still regresses after a short while
- repeated real-hardware `LOG_GET_EVENTS` responses still become malformed
- current malformed-host decode error is now commonly `missing required 'level'`
- latest local diagnostic firmware was confirmed to flash and run on hardware via temporary `VERSION.app_version` marker
- previous temporary markers progressed from `+usbtxretry1` to `+usbtxcoop1` to `+usbtxready1` to `+usbtxrqoff1` to `+usbsharedoff1` to `+hidhinitoff1`, and the newest root-cause send-timeout build now uses `+usbtxwait1`
- despite that confirmed flash, opening Debug still hard-freezes the keyboard
- with the latest flashed diagnostic build, the observed failure boundary moved again: `VERSION` still decodes, but even single-packet `LOG_GET_STATE` may now receive no response
- after changing `usb_comm` thread creation back to `K_PRIO_COOP(...)`, UF2 copy behavior recovered, but opening the host app can now freeze the keyboard before `VERSION` visibly completes
- with the newer `+usbtxready1` build, `VERSION` was briefly observed correctly on hardware, which proves the generic response TX path can succeed at least once
- after that success, attempting to change RGB lighting effect froze the keyboard
- after disconnect/reconnect following that freeze, `VERSION` was no longer readable again

## Most Important Current Finding

The latest raw packet analysis strongly suggests the corruption already exists in the firmware-encoded `LOG_GET_EVENTS` message before HID transport finishes the round trip.

Why this matters:

- outer `MessageD2H` framing is present
- first `log_events.events` submessage length prefix and body diverge
- later payload bytes drift into the wrong parse scope
- this points at firmware-side message construction/encode corruption, not frontend rendering

This is still a working conclusion, but it is evidence-based and narrower than the earlier hypothesis.

## Debugging Rules For The Next Window

Follow these rules strictly:

- do not treat a plausible explanation as the root cause until it is verified
- do not patch around symptoms just because they look suspicious
- trace the exact data flow and byte flow end-to-end before changing behavior
- verify where corruption first appears
- prefer instrumentation and proof over workaround patches
- if something is not yet proven, call it a hypothesis
- prioritize root-cause fixes over memory-expensive buffering or masking behavior
- when choosing what to suspect first, suspect code we changed before suspecting untouched code
- prefer suspecting our own recent edits before suspecting nanopb, Zephyr, or the compiler
- trust the compiler/toolchain by default; only investigate compiler/toolchain faults after normal code and data-flow causes have been exhausted
- if a workaround is added only for diagnosis, label it clearly and revisit/remove it after evidence is gathered

## What Was Added This Round

### 1. Documentation discipline updates

These files were updated so future windows inherit the stricter debugging discipline:

- `MIGRATION_HANDOFF.md`
- `MIGRATION_HANDOFF_PROMPT.md`

### 1b. Host trace retention across disconnect

File:

- `deps/zmkx.app/src/stores/usb.ts`

What it does:

- transport trace and transport stats are no longer cleared automatically on disconnect
- the user can still clear them explicitly with `Clear Trace`

Why:

- recent failures often require unplug/replug before the next request can be tried
- automatic trace clearing was destroying the most valuable evidence: the last successful request/response and the first failing request before disconnect
- retaining trace across disconnect is diagnostic-only host behavior and does not change firmware transport semantics

### 2. Firmware-side self-decode verification

File:

- `config/app/usb_comm/usb_comm_proto.c`

What it does:

- after encoding `LOG_GET_EVENTS` into `usb_tx_buf`, firmware immediately attempts `pb_decode_delimited()` on the same bytes
- if that self-decode fails, firmware replaces the response with a tiny sentinel `LOG_GET_EVENTS` packet instead of letting host only see a timeout

Why:

- to prove whether the bytes are already malformed before HID transport/host decode

Interpretation for the next window:

- if host receives a decodable sentinel `LOG_GET_EVENTS`, then the corruption happened before HID finished sending the original bytes
- if firmware self-decode passes but host still receives malformed bytes, then re-check HID packetization or host reassembly

### 3. Temporary flashed-build marker

Files:

- `config/app/usb_comm/handler/handler_version.c`

What it does:

- appends temporary suffix to `VERSION.app_version`
- current expected suffix is `+usbtxcoop1`
- previous suffix `+usbtxretry1` belonged to the earlier build where `usb_comm` thread had been changed to preemptive scheduling

Why:

- to prove the current local diagnostic UF2 actually reached hardware
- the git commit id alone was not enough, because local uncommitted firmware edits do not change the reported commit hash

Important:

- this is a temporary diagnostic marker
- remove or revisit it once transport diagnosis is complete
- future temporary diagnostic edits must also be recorded in handoff docs so context survives compression

### 4. Startup-active suspect revisited

Files:

- `config/app/usb_comm/usb_comm_proto.c`

What changed:

- `usb_comm` thread creation was changed to `K_PRIO_PREEMPT(...)` during diagnostics
- after reviewing which recent edits can affect startup-time USB behavior, that change was identified as a plausible confounder because it takes effect at boot, unlike request-time `usb_comm_hid_send()` changes
- thread creation has now been changed back to `K_PRIO_COOP(...)`

Why this matters:

- if the user-observed UF2 copy behavior changed right after a "low-level HID/USB" tweak, this preemptive startup-thread change is a much better suspect than the request-path HID send loop
- this does not prove it was the root cause, but it is the narrowest startup-active change we recently introduced ourselves

### 5. Generic HID TX-path suspect revisited

Files:

- `config/app/usb_comm/usb_comm_hid.c`
- `config/app/usb_comm/handler/handler_version.c`

What changed:

- `usb_comm_hid_send()` was changed back away from the active `hid_int_ep_write(...)/-EAGAIN` retry loop
- transmit gating now again follows the original `int_in_ready` callback plus semaphore model used by the upstream ZMK HID path
- temporary `VERSION.app_version` marker was advanced again so the next flashed build can be distinguished from `+usbtxcoop1`
- current expected suffix for this next build is `+usbtxready1`

Why this matters:

- after the `K_PRIO_COOP(...)` rollback, the earliest visible failure appears to have moved even earlier: opening the host app can now freeze the keyboard before `VERSION` is visibly shown
- that boundary implicates the generic response send path more directly than the previous `LOG_GET_STATE` / `LOG_GET_EVENTS` chain
- `usb_comm_hid_send()` is our own recent low-level change that affects every response, including `VERSION`
- local reference code in `deps/zmk/app/src/usb_hid.c` uses `int_in_ready` callback gating, so this is not a random redesign; it is a targeted rollback toward the known-good send model

Status:

- this is still a hypothesis-driven targeted test, not yet a proven root-cause fix
- preserve the existing `LOG_GET_EVENTS` self-decode verification while re-testing the earliest failure boundary on hardware

## Newly Proven Boundary Shift

The latest hardware report narrows the failure boundary again:

- `VERSION.app_version` with `+usbtxready1` was observed at least once
- this proves the flashed `usbtxready1` firmware is actually running
- this also proves the generic response path is not permanently dead from the very first request
- the next visible freeze happened later, when attempting RGB effect changes
- after that freeze and reconnect, `VERSION` stopped decoding again

What this means:

- the current earliest failure is no longer safely described as "before `VERSION`"
- but it is also not yet proven to be specifically `RGB_SET_STATE`
- entering RGB page automatically issues `RGB_GET_STATE` and `RGB_GET_INDICATOR`, and effect changes issue `RGB_SET_STATE`
- firmware-side generic request logging was still running for normal actions like `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR`
- next retest must distinguish which of those RGB-side requests is the first one after the last healthy `VERSION`

### 6. Generic USB request-log isolation test

Files:

- `config/app/usb_comm/usb_comm_proto.c`
- `config/app/usb_comm/handler/handler_version.c`

What changed:

- firmware-side generic `HW75_DIAG_EVENT_USB_REQUEST` logging for non-log USB requests was temporarily disabled
- this removes request-path diag writes for actions like `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR`
- temporary `VERSION.app_version` marker was advanced again so this isolation build can be distinguished from `+usbtxready1`
- current expected suffix for this next build is `+usbtxrqoff1`

Why this matters:

- code search confirmed RGB page does not send `LOG_*` host requests
- however, normal USB requests still passed through the log system because `usb_comm_handle_message()` had been logging every non-log request
- that made the log system a shared confounder even when the user never opened Debug
- this is a narrow isolation step that suspects our recent shared logging change before suspecting untouched RGB logic

Status:

- this is a temporary diagnostic rollback, not a proven fix
- keep `LOG_GET_EVENTS` self-decode verification and other transport diagnostics intact while testing it

### 7. Shared USB-layer log isolation test

Files:

- `config/app/usb_comm/usb_comm_proto.c`
- `config/app/usb_comm/usb_comm_hid.c`
- `config/app/usb_comm/handler/handler_version.c`

What changed:

- shared `usb_comm` transport code no longer writes diag-log events or snapshots from `usb_comm_proto.c`
- shared HID send path no longer writes diag-log events from `usb_comm_hid.c`
- `LOG_GET_EVENTS` self-decode verification and sentinel replacement remain in place because they do not require diag-ring writes in the normal request path
- temporary `VERSION.app_version` marker was advanced again so this stronger isolation build can be distinguished from `+usbtxrqoff1`
- current expected suffix for this next build is `+usbsharedoff1`

Why this matters:

- latest host trace showed zero RX packets even for `VERSION`, so the failure is still in the shared path before any debug-specific host decode matters
- disabling only generic request logging was not sufficient
- this broader isolation step removes remaining shared `usb_comm`-layer participation in the diag ring before suspecting untouched application handlers

Status:

- this is still a temporary diagnostic rollback, not a proven fix
- if `VERSION` still gets zero RX on this build, the next suspect moves even lower into shared USB/HID behavior rather than log-ring side effects

### 8. HID init event isolation test

Files:

- `config/app/hid_mouse.c`
- `config/app/usb_comm/handler/handler_version.c`

What changed:

- the `HW75_DIAG_EVENT_HID_INIT` startup event in `hid_mouse.c` was temporarily disabled
- HID snapshot update remains in place
- temporary `VERSION.app_version` marker was advanced again so this build can be distinguished from `+usbsharedoff1`
- current expected suffix for this next build is `+usbtxwait1`

Why this matters:

- latest trace shows shared RX has recovered enough to decode `VERSION`, `LOG_GET_STATE`, and a recovered `LOG_GET_EVENTS` batch
- the remaining corruption appears concentrated in the very first event of the event ring, while later startup events decode correctly
- based on current startup ordering and decoded events, the most plausible first event is the HID mouse init event we added ourselves
- this is a very narrow test of one likely-corrupt event source, not a redesign

Status:

- this is a temporary diagnostic rollback, not a proven fix
- if the first malformed event disappears on this build, focus shifts from transport framing to that specific event source

### 9. Single-event boundary retest

File:

- `deps/zmkx.app/src/stores/debug.ts`

What changed:

- host-side `LOG_GET_EVENTS` polling batch size was temporarily reduced from `4` back to `1`

Why this matters:

- latest `+hidhinitoff1` trace showed `VERSION` and `LOG_GET_STATE` decoding again, but `LOG_GET_EVENTS` still became the next failing stage
- `LOG_GET_STATE` reported `newest_seq=4`, so the first retest should now distinguish whether requesting only the first event gets any RX at all
- this is a stage-boundary diagnostic: single-event/single-packet versus multi-event/multi-packet

Status:

- temporary host-side diagnostic only
- revisit after the next hardware trace establishes the new earliest failure point

### 10. Root-cause finding on keyboard freeze

Files:

- `config/app/usb_comm/usb_comm_hid.c`
- `config/app/usb_comm/usb_comm_proto.c`

What code-path tracing proved:

- the later "no RX" symptom can be downstream of a firmware-side stall, not only a host-side decode issue
- `usb_comm_thread` currently runs as `K_PRIO_COOP(CONFIG_HW75_USB_COMM_THREAD_PRIORITY)`
- Zephyr `usb_write()` retries `-EAGAIN` up to `CONFIG_USB_NUMOF_EP_WRITE_RETRIES=10`, and each retry only does `k_yield()`
- in a cooperative thread, that retry loop can keep the same high-priority thread runnable and starve lower-priority keyboard work
- our current `usb_comm_hid_send()` ignores the return value from `k_sem_take(&hid_sem, K_MSEC(30))`
- that means our code can still call `hid_int_ep_write()` after the TX-ready wait has already timed out

Why this matters:

- this is a root-cause path inside the shared transport we changed, not a surface-level `LOG_*` or RGB symptom
- it matches the user-observed pattern where opening the host app can make the keyboard appear frozen, after which later responses stop

Immediate corrective step:

- make `usb_comm_hid_send()` fail fast on `hid_sem` timeout instead of writing anyway
- propagate that send failure out of `usb_comm_handle_message()` instead of silently ignoring it
- retest hardware from that narrowed fix before making any further protocol-shape changes

### 11. Deeper root cause: host overlaps requests against single-slot firmware RX

Files:

- `config/app/usb_comm/usb_comm_proto.c`
- `deps/zmkx.app/src/stores/usb.ts`
- `deps/zmkx.app/src/routes/Rgb.vue`
- `deps/zmkx.app/src/routes/Debug.vue`
- `deps/zmkx.app/src/pages/Main.vue`

What was proven by tracing code and latest `+usbtxwait1` trace:

- firmware request intake is single-slot:
  - one shared `usb_rx_buf`
  - one shared `usb_rx_len`
  - one shared `usb_rx_idx`
  - one `k_sem` initialized with max count `1`
- `usb_comm_handle_packet()` writes each completed host request into that single shared buffer and only does `k_sem_give(&usb_comm_sem)` when a message completes
- there is no request queue on firmware side, and no request ID correlation in the protocol
- host currently allows multiple request-response exchanges to overlap:
  - `Main.vue` auto-sends `VERSION`
  - `Rgb.vue` auto-sends `RGB_GET_STATE` and `RGB_GET_INDICATOR`
  - `Debug.vue` auto-starts `LOG_GET_STATE` and then `LOG_GET_EVENTS`
- latest trace with `+usbtxwait1` confirms overlapping host sends still happen in the same short window, even though the earlier HID TX-timeout bug was narrowed

Why this matters:

- this is a stronger root cause than any individual `LOG_*` or RGB symptom
- the host is violating the firmware's actual receive model
- that explains why the first visible failure boundary shifts between `VERSION`, `RGB_*`, and `LOG_*` depending on timing

Corrective direction:

- keep firmware receive model unchanged for now
- serialize host requests end-to-end so only one request may be outstanding until its response arrives or times out
- do not add larger firmware-side buffering unless serialization proves insufficient

## Current Key Files

- `config/app/usb_comm/usb_comm_proto.c`
- `config/app/usb_comm/handler/handler_debug_log.c`
- `config/proto/usb_comm.proto`
- `deps/zmkx.app/src/utils/usb/usb-hid.ts`
- `deps/zmkx.app/src/stores/debug.ts`
- `MIGRATION_HANDOFF.md`
- `MIGRATION_HANDOFF_NEXT.md`
- `MIGRATION_HANDOFF_PROMPT.md`

## Current Build State

### Firmware

Build succeeded after the shared USB-layer log isolation change and version-marker update.

Latest artifact:

- `build/keyboard12/zephyr/zmk.uf2`

Latest memory:

- `FLASH: 72320 B / 104 KB = 67.91%`
- `SRAM: 20184 B / 20 KB = 98.55%`

### Host

Host was not rebuilt in the very last round because the latest and current changes are firmware-only.

Previously built host artifact still exists:

- `deps/zmkx.app/dist/index.html`

## Evidence Gathered So Far

### Static size facts

- `usb_comm_LogEvent = 40 B`
- `usb_comm_LogEvents = 188 B`
- `usb_comm_MessageD2H = 192 B`
- `usb_comm_MessageH2D = 64 B`

### Static stack-usage facts

From `-fstack-usage` builds:

- `usb_comm_handle_message()` static stack: `328 B`
- `handle_log_get_events()` static stack: `56 B`
- `pb_encode()` static stack: `96 B`
- `pb_encode_submessage()` static stack: `48 B`

This does not prove stack overflow, but `CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE=1024` remains a valid thing to verify further with runtime instrumentation if needed.

## Immediate Next Step

The latest previously flashed local diagnostic UF2 was confirmed to boot on hardware, so the next step is no longer "did the flash succeed?".

Before the next hardware re-test, flash the newer build that reports `+usbtxwait1`.

Now verify the newest proven failure boundary on real hardware.

Specifically:

1. use the flashed build that reports `+usbtxwait1`
2. first verify whether `VERSION` still decodes on a fresh connection
3. if `VERSION` is healthy, capture the earliest RGB-side request boundary next
4. remember that entering RGB page automatically sends `RGB_GET_STATE` and `RGB_GET_INDICATOR`
5. then distinguish whether the freeze first appears on those automatic RGB requests or only on a later `RGB_SET_STATE` effect-change request
6. only after that RGB boundary is re-proven, return to checking Debug page and `LOG_GET_STATE`
7. with `+usbtxwait1`, use the temporary single-event `LOG_GET_EVENTS` polling path to test whether the first event gets any RX at all
8. if `VERSION` stops appearing even on a fresh connection, treat the generic response path as regressed again and capture that earlier boundary instead

## What Not To Do Next

- do not start TouchBar gesture debugging
- do not restore `boot_events`
- do not restore `snapshots`
- do not do speculative SRAM optimization yet
- do not keep layering host-side recovery hacks unless they prove a stage boundary

## What To Report Back Next Window

- whether `VERSION.app_version` shows `+usbtxwait1`
- whether the older `+usbsharedoff1` marker is definitely gone from hardware after reflashing
- whether `VERSION` itself got zero RX packets, malformed RX data, or a timeout
- whether RGB freeze first appeared on `RGB_GET_STATE`, `RGB_GET_INDICATOR`, or `RGB_SET_STATE`
- whether `LOG_GET_STATE` received any RX packet bytes at all after `VERSION` once `VERSION` was healthy again
- whether the keyboard freeze happens immediately on opening Debug or only after the first debug request
- whether the temporary version marker was removed or still present
- whether firmware self-decode sentinel was observed on host
- whether `LOG_GET_EVENTS` ever shows clean `rx-decode`
- exact new trace lines
- any newly proven stage boundary for where bytes first become malformed
- files changed
- whether firmware build ran
- whether host build ran
- current risks

## 2026-04-05 RGB Wake Boundary Update

New hardware evidence changed the failure boundary:

- binary debug log transport is now healthy after the host-side global request serialization fix
- repeated `LOG_GET_STATE` and `LOG_GET_EVENTS` can succeed for long stretches
- the user later discovered those "healthy" runs happened while RGB was not visibly lit
- board config has `CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=y`, so idle-dark runs can hide RGB refresh activity entirely
- after idle sleep and a wake keypress, RGB becomes visibly active again
- if the host app is started while RGB is already visibly on, the lighting freezes immediately and the keyboard hard-locks

That means the current earliest proven trigger is now:

- active RGB refresh + host USB traffic

Current evidence-based hypothesis:

- the shared failure path is likely in active underglow refresh, not generic `LOG_*` framing
- `deps/zmk/app/src/rgb_underglow.c` drives full-strip refresh on each underglow tick
- the local code trace identified `irq_lock()` around `led_strip_update_rgb()` as the strongest shared-risk boundary once RGB is active
- on this board the chain is 103 LEDs, so a full synchronous SPI refresh is long enough to be a credible interrupt-starvation source

Temporary local diagnostic change already staged:

- remove `irq_lock()/irq_unlock()` around `led_strip_update_rgb()` in `deps/zmk/app/src/rgb_underglow.c`
- temporary firmware marker: `+rgbirqoff1`

This is a narrow diagnostic change, not a redesign. The next hardware question is:

- does the hard freeze disappear when RGB refresh no longer disables interrupts across the full LED SPI transfer?

## 2026-04-05 RGB Handler Log Isolation

The `+rgbirqoff1` diagnostic did not resolve the freeze, and the user correctly pointed out that changing original underglow logic is not the right long-term direction unless the evidence is overwhelming.

That diagnostic is now backed out locally. Current staged diagnostic is narrower and only touches our own added RGB-side logging path:

- restore the original `deps/zmk/app/src/rgb_underglow.c` logic
- remove `hw75_diag_log_event()` / `hw75_diag_update_snapshot()` calls from `config/app/usb_comm/handler/handler_rgb.c`
- temporary firmware marker: `+rgblogoff1`

Why this is now the most evidence-based next cut:

- `RGB_GET_STATE` already decodes successfully before the freeze
- the failing path is `RGB_CONTROL` / later RGB mutation requests
- compared with a working `RGB_GET_STATE`, the additional path difference includes the original ZMK RGB state mutation plus our own RGB diagnostic logging
- before blaming untouched vendor RGB code, we should first isolate the RGB diagnostic writes we added ourselves

Next hardware question:

- if `+rgblogoff1` still freezes on RGB toggle, the root cause is probably not RGB diagnostic-event emission itself
- if `+rgblogoff1` no longer freezes, the root cause is in our added RGB log/snapshot path interacting with active RGB refresh

## 2026-04-05 USB TX Failure Telemetry

`+rgblogoff1` still did not change the freeze.

Current conclusion:

- RGB handler diagnostic logging is not the root cause by itself
- the next most likely shared boundary remains the custom USB response send path that we changed
- specifically, if `RGB_CONTROL` reaches response encode but the HID IN path stalls, the cooperative USB comm thread can still wedge the board before the host sees any reply

New temporary diagnostic staged locally:

- add failure-only `hw75_diag_log_event()` telemetry in `config/app/usb_comm/usb_comm_hid.c`
- emit `HW75_DIAG_EVENT_USB_TX_WAIT_TIMEOUT` on `k_sem_take(&hid_sem, ...)` timeout
- emit `HW75_DIAG_EVENT_USB_TX_WRITE_FAIL` if `hid_int_ep_write()` fails
- emit `HW75_DIAG_EVENT_USB_TX_SHORT_WRITE` if the write byte count is corrupted
- temporary firmware marker: `+usbtxdiag1`

This does not change the send behavior yet. It only exposes the exact send-failure boundary through the existing event ring so the next reproduction can answer whether the board dies in the custom USB TX path.

## 2026-04-05 Runtime Stack Watermark Diagnostic

`+usbtxdiag1` still did not surface any explicit `USB_TX_WAIT_TIMEOUT`, `USB_TX_WRITE_FAIL`, or `USB_TX_SHORT_WRITE` evidence before the freeze.

That weakens the "custom USB TX path explicitly fails first" hypothesis.

Current next-step diagnostic is now a direct runtime resource check:

- enable `CONFIG_THREAD_STACK_INFO=y`
- enable `CONFIG_INIT_STACKS=y`
- record only new low-water marks, to avoid spamming the ring

Instrumentation currently staged locally:

- `config/app/usb_comm/usb_comm_proto.c`
  - log `HW75_DIAG_EVENT_USB_STACK_WATERMARK` when the USB comm thread reaches a new minimum free-stack watermark
  - stage `1`: after request decode / before handler
  - stage `2`: after handler returns
  - stage `3`: after response encode
- `deps/zmk/app/src/rgb_underglow.c`
  - log `HW75_DIAG_EVENT_RGB_WORKQ_STACK_WATERMARK` when the lowprio RGB workqueue thread reaches a new minimum free-stack watermark
  - stage `1`: underglow tick entry
  - stage `2`: after `led_strip_update_rgb()`
- host decode in `deps/zmkx.app/src/stores/debug_decode.ts` renders these numeric event ids directly
- temporary firmware marker: `+stackdiag1`

Why this is evidence-based:

- current SRAM headroom is extremely small
- `+stackdiag1` now builds successfully, and that build reports `SRAM 20272 B / 20 KB = 98.98%`, leaving only about `208 B` static headroom
- the failure survives removal of RGB handler diagnostic-event writes
- the failure survives failure-only USB TX telemetry without producing a TX-fail event
- so stack/resource exhaustion is now a stronger shared-cause candidate than the previously tested interface-level hypotheses

Additional local evidence gathered after staging `+stackdiag1`:

- `About` / version fetch itself only sends `VERSION`; it does not issue `LOG_*`
- Debug polling is started and stopped by `deps/zmkx.app/src/routes/Debug.vue`, so any trace showing repeated `LOG_GET_EVENTS` still means the Debug route is active
- `config/app/diag_log.c` uses a `k_spinlock` around ring updates, so there is no obvious unlocked multi-thread ring corruption in the added diagnostic store itself
- large static SRAM consumers in the current `+stackdiag1` build include:
  - `ws2812_spi_0_px_buf` = `0x9a8` (`2472 B`)
  - `diag_state` = `0x2d4` (`724 B`)
  - `usb_comm_thread_stack` = `0x400` (`1024 B`)
  - `sys_work_q_stack` = `0x400` (`1024 B`)
  - `lowprio_q_stack` = `0x300` (`768 B`)

## 2026-04-05 Duplicated LOG_GET_EVENTS Buffer Removal

The user clarified that an earlier binary-log migration build was around `93%` SRAM, and the later rise happened after additional log-system work.

Local root-cause tracing identified one concrete self-inflicted source of stack pressure:

- `config/app/usb_comm/handler/handler_debug_log.c` already stores fetched events in `static struct log_encode_context ctx.events[4]`
- but `config/proto/usb_comm.proto` also defined `LogEvents.events` as `FT_STATIC`
- that made every stack-local `usb_comm_MessageD2H` carry another 4-event static array, even for non-log responses like `VERSION`

Measured generated-struct sizes before the change:

- `usb_comm_LogEvents` = `188 B`
- `usb_comm_MessageD2H` = `192 B`

Current local fix:

- remove the static repeated-array storage from `LogEvents.events`
- encode events directly from the existing `ctx.events[]` buffer via nanopb callback
- new temporary firmware marker: `+stackcb1`

Measured generated-struct sizes after the change:

- `usb_comm_LogEvents` = `32 B`
- `usb_comm_MessageD2H` = `68 B`

Important note:

- this change reduces runtime stack usage in the USB comm thread
- it does **not** reduce the reported SRAM percentage yet, because the build report counts reserved thread stacks/BSS, not how much of the 1024-byte USB comm stack is actually consumed at runtime

## 2026-04-05 Diagnostic Rollback After Root-Cause Confirmation

Hardware verification with `+stackcb2` confirmed:

- RGB and normal log transport run again after removing the duplicated `LOG_GET_EVENTS` event array from `usb_comm_MessageD2H`
- stack watermark still reached `32 B` free at `LOG_GET_EVENTS` stage 3, but the board no longer hard-locked

With the root-cause fix proven, the temporary diagnostics have now been rolled back from the default build:

- removed `LOG_GET_EVENTS` self-decode verification / sentinel replacement from `config/app/usb_comm/usb_comm_proto.c`
- removed failure-only `USB_TX_WAIT_TIMEOUT` / `USB_TX_WRITE_FAIL` / `USB_TX_SHORT_WRITE` event emission from `config/app/usb_comm/usb_comm_hid.c`
- removed temporary `+stackcb2` version suffix from `config/app/usb_comm/handler/handler_version.c`

Stack watermark code was kept in source but disabled in the default firmware config:

- removed `CONFIG_THREAD_STACK_INFO=y`
- removed `CONFIG_INIT_STACKS=y`
- this avoids the extra SRAM/runtime overhead in normal builds
- the watermark instrumentation can be re-enabled later if needed

Current clean-build memory after rollback:

- `FLASH: 71572 B / 104 KB = 67.21%`
- `SRAM: 20248 B / 20 KB = 98.87%`

## 2026-04-05 Boot Events Restored First

With the compact transport baseline healthy again, the next planned feature restore has been applied:

- `boot_events` are restored in `LOG_GET_STATE`
- `snapshots` remain disabled so the compact state response still stays small

Concrete local changes:

- `config/app/usb_comm/handler/handler_debug_log.c`
  - stop zeroing `ctx.boot_event_count`
  - keep zeroing `ctx.snapshot_count`
- `deps/zmkx.app/src/routes/Debug.vue`
  - update the Boot Events placeholder to reflect "no boot events yet" instead of claiming the feature is disabled

Build status after this change:

- firmware build ran and succeeded
- host build ran and succeeded

Current memory remains:

- `FLASH: 71568 B / 104 KB = 67.20%`
- `SRAM: 19672 B / 20 KB = 96.05%`

Next hardware verification:

1. flash the current UF2
2. open Debug
3. confirm Boot Events now shows the expected startup entries
4. confirm Snapshots still remain empty/disabled
5. confirm `VERSION`, `RGB`, `LOG_GET_STATE`, and `LOG_GET_EVENTS` still behave normally

## 2026-04-05 Boot Events Encode Regression

The first attempt to restore `boot_events` exposed a new deterministic failure boundary:

- keyboard stayed alive
- `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR` remained healthy
- `LOG_GET_STATE` responses became malformed on host
- host trace showed repeated `invalid wire type 4 at offset 53`
- the malformed queue bytes proved the broken area was inside the `boot_events` region of `LOG_GET_STATE`, not generic packet framing

Evidence-based local conclusion:

- this is the same class of problem as the earlier `LOG_GET_EVENTS` stack/encoding issue
- `boot_events` callback was still building each `LogEvent` via a stack-local temporary protobuf struct during encode
- `LOG_GET_EVENTS` had already been fixed by pre-filling protobuf structs and encoding them directly
- `boot_events` needed the same treatment

Current local fix:

- change `ctx.boot_events[]` in `handler_debug_log.c` from `hw75_diag_event` to `usb_comm_LogEvent`
- pre-fill each boot event with `fill_log_event()`
- encode boot events directly with `pb_encode_submessage(..., &ctx.boot_events[i])`
- temporary firmware marker: `+bootcb1`

Build status:

- firmware build succeeded
- host build did not need to run for this fix

Current build memory:

- `FLASH: 71600 B / 104 KB = 67.23%`
- `SRAM: 19736 B / 20 KB = 96.37%`

## 2026-04-05 Boot Events Follow-Up: Empty Snapshots Callback Suspect

`+bootcb1` did not fix the malformed `LOG_GET_STATE` bytes. New evidence narrowed the structure boundary further:

- keyboard remained alive
- `VERSION`, `RGB_GET_STATE`, and `RGB_GET_INDICATOR` still decoded
- `LOG_GET_STATE` still returned the same deterministic malformed 94-byte response
- packet boundary analysis showed the missing/corrupted bytes were not at the 62/32 HID packet split
- this means the corruption is happening before or during protobuf encode, not during host packet reassembly

The next narrow hypothesis is structural rather than packet-level:

- `LOG_GET_EVENTS` has one populated callback field and is healthy
- `LOG_GET_STATE` currently has two callback fields in sequence
- `snapshots` is intentionally disabled (`ctx.snapshot_count = 0U`) but the code was still attaching an empty snapshots callback ahead of the populated `boot_events` callback
- that empty-then-populated callback sequence is now the most suspicious remaining difference in the encode path

Current local change:

- `handler_debug_log.c`
  - only attach `state->snapshots` callback when `ctx.snapshot_count > 0`
  - only attach `state->boot_events` callback when `ctx.boot_event_count > 0`
- `usb_comm_proto.c`
  - remove the unfinished self-decode helper remnants that were not actually wired in
- temporary firmware marker: `+bootnosnapcb1`

Build status after this change:

- firmware build succeeded
- host build was not needed

Current build memory:

- `FLASH: 71488 B / 104 KB = 67.13%`
- `SRAM: 19736 B / 20 KB = 96.37%`

Next hardware report should state explicitly:

1. whether `VERSION.app_version` shows `+bootnosnapcb1`
2. whether `LOG_GET_STATE` still returns malformed RX data, or decodes cleanly again
3. the exact new raw trace lines if it still fails

## 2026-04-05 LOG_GET_STATE Source Probe

`+bootnosnapcb1` still produced the exact same malformed 94-byte `LOG_GET_STATE` response, so the empty `snapshots` callback theory was not sufficient.

That means current host-side tracing is no longer enough by itself: it proves the received bytes are wrong, but it does not prove where they first become wrong.

Current local diagnostic enhancement is deliberately narrow and evidence-oriented:

- in `handler_debug_log.c`
  - encode `boot_event[0]` by itself as a standalone `usb_comm_LogEvent`
  - log its encoded bytes `[0..15]` into the normal event ring
  - log `LOG_GET_STATE` meta (`oldest_seq`, `newest_seq`, boot-event count, snapshot count)
- in `usb_comm_proto.c`
  - after `LOG_GET_STATE` is fully encoded, log response bytes `[11..18]` and `[19..26]`
- in host `debug_decode.ts`
  - decode these temporary diagnostic events into readable byte strings
- temporary firmware marker: `+stateprobe1`

Why this is the right next step:

- if standalone `boot_event[0]` bytes are already missing the leading fields, the source event-to-protobuf conversion is wrong
- if standalone `boot_event[0]` encodes correctly, but the final `LOG_GET_STATE` bytes are wrong, then the corruption is introduced while composing `LogState`
- if final firmware-side response bytes already match the malformed host bytes, USB send/receive is exonerated and the bug is definitively pre-transport

Next hardware report should include:

1. whether `VERSION.app_version` shows `+stateprobe1`
2. the new `USB Comm` diagnostic events 112-116 from `LOG_GET_EVENTS`
3. whether those firmware-side response bytes match the malformed host raw trace

## 2026-04-05 Host Probe Fetch Fix

The first `+stateprobe1` hardware trace showed why events 112-116 were missing:

- host still sent `LOG_GET_STATE`
- `LOG_GET_STATE` still malformed deterministically
- but `startPolling()` stops immediately on that startup failure
- therefore host never reaches the first `LOG_GET_EVENTS` fetch, even though the firmware-side probe events were already appended to the ring

So the firmware probe itself is still valid, but host was aborting before reading it.

Current local host change:

- `deps/zmkx.app/src/stores/debug.ts`
  - if startup `LOG_GET_STATE` fails, do one best-effort `LOG_GET_EVENTS` fetch in `finally`
  - preserve the original startup error, but still pull the probe events from the healthy event ring

Build status after this change:

- host build succeeded
- firmware did not need to change again for this step

## 2026-04-05 Root Cause Narrowed To TX Packet Buffer Reuse

The first useful `+stateprobe1` evidence finally separated encode from send:

- firmware-side `LOG_GET_STATE response bytes[11..18]` were logged as a valid protobuf sequence
- host still received a malformed multi-packet `LOG_GET_STATE`
- single-packet responses (`VERSION`, `RGB_GET_STATE`, `RGB_GET_INDICATOR`, `LOG_GET_EVENTS`) remained healthy
- the failing response size was 94 bytes, i.e. exactly a two-packet HID response

This points away from protobuf composition and toward the multi-packet TX path itself.

Root-cause code path:

- `usb_comm_hid_send()` uses one static `tx_buf`
- after the first `hid_int_ep_write()`, the loop immediately prepared the next packet into the same `tx_buf`
- only after overwriting the buffer did it wait on `hid_sem` for the endpoint to become ready again
- if `hid_int_ep_write()` is asynchronous with respect to caller-owned memory, that means packet N+1 can overwrite packet N while packet N is still in flight

Why this matches the observed behavior:

- corruption only appears on multi-packet responses
- firmware-side encoded bytes are correct before send
- host-side bytes become corrupted afterward
- the corruption is deterministic because the second packet preparation deterministically rewrites the same static buffer

Current local fix:

- `config/app/usb_comm/usb_comm_hid.c`
  - move `k_sem_take(&hid_sem, ...)` before filling `tx_buf`
  - now the shared packet buffer is not rewritten until the previous IN transfer has completed
- temporary firmware marker: `+txbuffix1`

Next hardware report should state explicitly:

1. whether `VERSION.app_version` shows `+txbuffix1`
2. whether `LOG_GET_STATE` now decodes successfully
3. whether Boot Events render normally
4. whether the temporary probe events still need to be kept or can be removed

## 2026-04-05 TX Buffer Root Cause Confirmed And Probe Cleaned Up

Hardware confirmed the `+txbuffix1` fix:

- `LOG_GET_STATE` decodes successfully again
- `Boot Events` render normally
- transport remains healthy under normal polling

This confirms the root cause was the shared static HID TX packet buffer being rewritten for packet N+1 before packet N had actually finished sending.

Cleanup applied after confirmation:

- removed temporary `LOG_GET_STATE` source-probe events 112-116
- removed the temporary host-side best-effort `LOG_GET_EVENTS` fetch after startup `LOG_GET_STATE` failure
- removed the temporary `+txbuffix1` version suffix
- kept the actual fix in `config/app/usb_comm/usb_comm_hid.c`:
  - wait on `hid_sem` before filling the shared `tx_buf`

Current status after cleanup:

- root-cause fix retained
- temporary diagnostics removed
- `boot_events` remain restored
- `snapshots` remain disabled and are the next planned restore step

## 2026-04-05 Snapshots Restored Locally

With the multi-packet TX buffer reuse bug fixed, `LOG_GET_STATE` no longer needs to force
`snapshot_count = 0`.

Current local change:

- `config/app/usb_comm/handler/handler_debug_log.c`
  - restore `snapshots` encoding in `LOG_GET_STATE`
  - keep the existing callback-based snapshot/boot-event encoding path
- `deps/zmkx.app/src/routes/Debug.vue`
  - update the empty-state copy to `No snapshots captured yet.`

Expectation for the next hardware check:

1. `VERSION`, `RGB`, `LOG_GET_STATE`, and `LOG_GET_EVENTS` should remain healthy
2. the `Snapshots` card should populate again instead of showing the temporary-disabled text
3. if `LOG_GET_STATE` grows beyond the current `hw75_keyboard` TX limit, firmware should report
   `LOG_EVENT_USB_RESPONSE_LARGE` rather than silently corrupting the response

## 2026-04-05 Snapshot Restore Verified On Hardware

Hardware verification succeeded after restoring `snapshots`:

- `LOG_GET_STATE` remained healthy
- `LOG_GET_EVENTS` remained healthy
- `Boot Events` rendered correctly
- `Snapshots` rendered correctly
- `RGB` remained healthy
- transport stayed stable under polling

Observed snapshot set on hardware:

- `System`
- `HID`
- `Indicator`

This means the immediate transport-stabilization goal for the binary debug-log path is now met:

- multi-packet TX corruption fixed
- `boot_events` restored
- `snapshots` restored
- no keyboard freeze during normal debug polling

Next planned focus can return to TouchBar-specific migration/debugging, using the now-stable
binary log transport.

## 2026-04-05 TouchBar Init Root Cause Narrowed

The first TouchBar-specific failure is still:

- `TouchBar initialization failed because the chosen kscan device was not ready.`

But the current evidence now points to init ordering rather than a permanently broken device:

- Boot Events show `TOUCHBAR_INIT_FAIL` before `KSCAN_TOUCH_MAP`
- `touchbar_init()` currently runs at `APPLICATION / CONFIG_APPLICATION_INIT_PRIORITY`
- the `kscan-gpio-74hc165` device is also registered at `APPLICATION / CONFIG_APPLICATION_INIT_PRIORITY`
- therefore `touchbar_init()` can run before the chosen `zmk,kscan` device has finished its own
  device init, making `device_is_ready()` fail transiently

Current local fix:

- `config/app/touchbar.c`
  - move `touchbar_init()` to `APPLICATION / (CONFIG_APPLICATION_INIT_PRIORITY + 1)`
  - this keeps the same init stage but guarantees TouchBar comes after the chosen kscan device

Next hardware check should confirm:

1. whether the boot-time `TOUCHBAR_INIT_FAIL` event disappears
2. whether TouchBar snapshot/logging begins without the old startup failure

## 2026-04-05 Touch Map Decode Fix (Host)

After init-ordering was fixed, the Debug page still rendered obviously bogus values such as
`r26/c161` for `KSCAN_TOUCH_MAP`.

Root cause:

- firmware intentionally packs each TouchBar row/column into 4-bit nibbles
- host decode in `debug_decode.ts` was reading them back with 8-bit extraction (`u8(...)`)
- that made adjacent row/column nibbles bleed together in the rendered text

Current local fix:

- `deps/zmkx.app/src/stores/debug_decode.ts`
  - add nibble extraction helper
  - decode `LOG_EVENT_KSCAN_TOUCH_MAP` using 4-bit fields instead of 8-bit fields

This is an observability-only fix; firmware behavior is unchanged.

## 2026-04-05 TouchBar Init Verified On Hardware

Hardware now reports the expected startup sequence:

- `Indicator` init
- `System` diag init
- `KSCAN_TOUCH_MAP`
- `TOUCHBAR_INIT`

Observed boot log:

- `TouchBar logical map is 0=r10/c1 1=r10/c3 2=r10/c5 3=r10/c0 4=r10/c2 5=r10/c4.`
- `TouchBar polling initialized with 10 ms interval.`

This confirms:

- the init-order fix worked
- the chosen `zmk,kscan` device is now ready before `touchbar_init()`
- host-side map decoding is now sane

Next focus is no longer init, but live touch behavior:

1. verify raw/debounced/logical `KSCAN_TOUCH_STATE` changes while touching the strip
2. verify `TOUCH_START`, `GESTURE_ACTIVE`, and `TOUCH_END`
3. only after input is confirmed, debug higher-level gesture behavior if needed

## 2026-04-05 TouchBar Live Trace Verified, Ring Drop Root Cause Found

Live touch tracing is now working:

- `KSCAN_TOUCH_STATE` transitions appear as expected
- `TOUCH_START`, `GESTURE_ACTIVE`, release-grace, and `TOUCH_END` all appear
- `Pan` mode emits HID wheel steps with sensible signed values

The remaining `Dropped: N` shown in the Debug header is not a transport drop:

- `Transport Trace` still shows `Dropped: 0`
- device-side debug header shows `Dropped > 0`
- `CONFIG_HW75_DIAG_LOG_RING_SIZE` is still only `12`
- host `debug.ts` was still left in diagnosis mode with `MAX_BATCH = 1`

So the current root cause for header-side drops is:

- TouchBar emits bursts of debug events faster than the host drains them
- because the host was still requesting only one event per `LOG_GET_EVENTS`

Current local fix:

- `deps/zmkx.app/src/stores/debug.ts`
  - restore `MAX_BATCH` from `1` back to `4`

This is a root-cause cleanup of the earlier diagnostic throttle, not a protocol workaround.

## 2026-04-05 Pan Trace Analysis

The current TouchBar log shows the Pan pipeline is working end-to-end:

- init is healthy
- raw -> debounced touch-state transitions are healthy
- `TOUCH_START`, activation delay, `GESTURE_ACTIVE`, release grace, and `TOUCH_END` are healthy
- HID wheel output is healthy

Behavioral interpretation from the trace:

- right-side touches (`mask 0x01`, segment 1, position 768) produce the expected segment-1 start
- left-side touches (`mask 0x20`, segment 0, position 0) produce the expected segment-0 start
- mid-strip touches (`mask 0x08`, segment 0, position 512) activate cleanly and then emit wheel
  steps as contact moves across adjacent logical positions
- the brief contact loss / recontact within release grace is handled as one continuous gesture,
  which is consistent with the current grace-window design

One host observability issue remained:

- wheel/step directions are packed as signed 8-bit values in firmware
- host was still decoding them as unsigned bytes, showing values like `254/252/250`

Current local host fix:

- `deps/zmkx.app/src/stores/debug_decode.ts`
  - decode HID wheel and TouchBar step directions as signed 8-bit values

Next TouchBar debug focus:

1. validate `tb_mode` mode cycling
2. verify App Switch mode behavior/logs
3. verify Desktop Switch mode behavior/logs

## 2026-04-05 App/Desktop Edge-Step Root Cause And Fix

New evidence from the App Switch / Desktop Switch traces shows the main remaining behavior bug is
not transport, init ordering, or half-segment selection.

What the logs proved:

- the active half-segment selection is already sticky per gesture and matches the intended
  overlapping layout:
  - left virtual strip uses logical positions `0/1/2/3`
  - right virtual strip uses logical positions `2/3/4/5`
  - ownership is decided on touch start and then held for the gesture
- the bad behavior happens after segment ownership is already chosen
- edge-start gestures such as:
  - `0x20 -> 0x30 -> 0x10` on the left edge
  - `0x01 -> 0x21 -> 0x20` on the right edge
  were being interpreted as real inward travel
- because App/Desktop step logic was still using:
  - one fixed gesture anchor (`anchor_position`)
  - direct `displacement / step_distance` quantization
- that let adjacent-pad redistribution or release-order jitter trigger:
  - premature opposite-direction steps from an edge start
  - `+1 -> -1` oscillation around one threshold in the middle of a gesture

Current local fix in `config/app/touchbar.c`:

- keep `anchor_position` as the original gesture anchor for Pan / desktop pre-seek finalize
- add a separate `step_anchor_position` for App/Desktop discrete step quantization
- ratchet `step_anchor_position` after each normal App/Desktop step
  - this removes the old fixed-anchor oscillation path
- keep edge-repeat behavior by moving `step_anchor_position` outward only for repeated edge steps
- add `step_entry_edge_direction`
  - if a gesture starts at the far left/right edge, the first inward step is suppressed until the
    contact has clearly left that edge zone
  - this specifically targets the user-observed "still on the edge but it switched the other way"
    failure mode

Why this is the root-cause fix rather than a parameter tweak:

- the old code already matched the original firmware constants and segment maps
- the failure came from how current migrated code interpreted noisy edge-state redistribution after
  ownership had already been chosen
- the new fix changes the step model itself, not just thresholds

Build status after this change:

- firmware build succeeded
- host build was not needed

Current memory after rebuild:

- `FLASH: 71732 B / 104 KB = 67.36%`
- `SRAM: 19744 B / 20 KB = 96.41%`

Next hardware verification should focus on:

1. App Switch:
   - hold on far left / far right and verify it no longer emits the first opposite-direction step
     too eagerly
   - move across one threshold in the middle and verify it no longer chatters `+1/-1/+1/-1`
2. Desktop Switch:
   - verify edge hold continues in the held edge direction
   - verify left-edge continuous left switching is now easier to trigger
3. if any bad step remains, capture the new logs and compare them specifically against:
   - `step_anchor_position` ratcheting expectation
   - edge-entry lock expectation

## 2026-04-05 TouchBar Logical Mapping Root Cause

After the App Switch retest, the user-reported physical behavior still did not match the logs:

- left-side touches often produced logs but did not make the App Switch UI appear
- the UI tended to appear only after moving much farther right
- once the UI was active, dragging back left partly worked, but the far-left edge still behaved
  incorrectly

That pattern pointed back to a more basic root cause than gesture thresholds:

- the six TouchBar channels were not being interpreted in the same logical left-to-right order as
  the original firmware

What was verified:

- original firmware `HelloWord/hw_keyboard.cpp` uses:
  - `RAW_BIT_BY_LOGICAL_POSITION = {0, 5, 4, 3, 2, 1}`
- current migrated driver had been exporting TouchBar logical positions from
  `matrix-transform` entries `82..87`
- on the current board overlay those entries are ordered:
  - `RC(10,1) RC(10,3) RC(10,5) RC(10,0) RC(10,2) RC(10,4)`
- that produces the previously logged map:
  - `0=r10/c1 1=r10/c3 2=r10/c5 3=r10/c0 4=r10/c2 5=r10/c4`
- this does not match the old firmware’s left-to-right logical order

So the stronger root cause is:

- TouchBar logical channel mapping was wrong before higher-level gesture code even ran

Current local fix in `config/drivers/kscan/kscan_gpio_74hc165.c`:

- stop deriving TouchBar logical order from `matrix-transform` positions `82..87`
- hard-code the board’s TouchBar logical mapping to match the original firmware:
  - rows: `10 10 10 10 10 10`
  - cols: `0 5 4 3 2 1`

Expected new startup diagnostic after flashing:

- `TouchBar logical map is 0=r10/c0 1=r10/c5 2=r10/c4 3=r10/c3 4=r10/c2 5=r10/c1.`

Why this change is evidence-based:

- it matches the original working firmware exactly
- it directly explains the user’s physical report that the “left half” only starts behaving after
  touching much farther to the right

Build status after this change:

- firmware build succeeded
- host build was not needed

Current memory remains:

- `FLASH: 71732 B / 104 KB = 67.36%`
- `SRAM: 19744 B / 20 KB = 96.41%`

Next hardware verification should check in this order:

1. startup `KSCAN_TOUCH_MAP` now matches the expected old-firmware order above
2. a single touch on the physical far-left side now logs as the logical far-left channel
3. App Switch UI can be brought up from the intended left-half/right-half regions without first
   sliding deep into the opposite side

## 2026-04-05 Final TouchBar Mapping Verification

The previous "restore old firmware order" hypothesis was not the final answer.

A temporary diagnostic build mapped TouchBar logical channels onto the key-backlight LEDs under
number-row keys `1..6`, and hardware verification showed:

- physical pad `1 -> logical LED 3`
- physical pad `2 -> logical LED 6`
- physical pad `3 -> logical LED 4`
- physical pad `4 -> logical LED 2`
- physical pad `5 -> logical LED 1`
- physical pad `6 -> logical LED 5`

From that verified board-level observation, the actual physical left-to-right TouchBar source
order is:

- row `10`
- cols `4, 1, 3, 5, 0, 2`

Current code now reflects that in `config/drivers/kscan/kscan_gpio_74hc165.c`.

Important:

- the temporary LED diagnostic overlay has already been removed again
- the mapping fix remains
- the user reported the resulting TouchBar behavior is now much more natural and timing feels OK

Current expected state for the next window:

1. do not revert the `4, 1, 3, 5, 0, 2` mapping
2. do not resurrect the temporary LED diagnostic unless another physical-mapping check is needed
3. treat remaining TouchBar work as higher-level gesture refinement only if the user asks for it

## 2026-04-09 USB crash root cause is now closed

The later "open RGB + Debug and the board still crashes" report was traced again from runtime
evidence instead of guessing at thread sizes.

What was re-enabled:

- `CONFIG_THREAD_STACK_INFO=y`
- `CONFIG_INIT_STACKS=y`
- USB comm low-water logging in `config/app/usb_comm/usb_comm_proto.c`
- RGB workqueue low-water logging in `config/app/rgb_effects.c`

One hidden source bug was exposed immediately:

- `usb_comm_log_stack_watermark()` in `usb_comm_proto.c` had never really been compiled before
- once the conditional path became active, it failed because `diag_log.h` was missing there
- that include is now fixed, so the watermark logs are real

The decisive hardware evidence before the runtime fix was:

- even `VERSION` reported:
  - `USB comm stack crossed the 50/70/90/100% used watermark at stage 1`
  - with `0 B free of 1024 B`

Why that matters:

- stage `1` is logged before the request handler runs
- so the overflow could not come from a specific handler
- it had to come from the common `usb_comm_handle_message()` frame

That frame was then measured directly:

- `usb_comm_MessageH2D = 312 B`
- `usb_comm_MessageD2H = 300 B`
- `pb_istream_t = 16 B`
- `pb_ostream_t = 20 B`

Verified root cause:

- `config/app/usb_comm/usb_comm_proto.c`
  - `usb_comm_handle_message()` was allocating both protobuf message unions as stack locals
  - on a `1024 B` worker stack, that was enough to overflow before entering even lightweight
    handlers like `VERSION`

Verified fix:

- move those two protobuf objects out of the thread stack into reused static scratch storage:
  - `usb_h2d_msg`
  - `usb_d2h_msg`
- this is safe because USB comm request handling is serialized on one worker thread
- this is the root-cause fix, not a blind stack-size increase

Current verified post-fix runtime evidence:

- USB comm no longer overflows at `VERSION / stage 1`
- latest observed USB low-water:
  - `LOG_GET_STATE`
  - `stage 3`
  - `296 B free of 1024 B`
  - about `71% used`

Important interpretation:

- the linker SRAM percentage increased after this change because the protobuf scratch moved into
  `.bss`
- that does **not** mean the runtime fix regressed safety
- it means the old dangerous stack peak is now represented as explicit static storage

Current build state after this fix:

- firmware build succeeded
- latest report:
  - `FLASH 73824 B / 104 KB = 69.32%`
  - `SRAM 19912 B / 20 KB = 97.23%`
- current artifact:
  - `build/keyboard12/zephyr/zmk.uf2`

So for the next window:

- do **not** restart from "maybe USB thread stack size is the main problem"
- the already verified crash root cause was the stack-local protobuf request/response pair
- if a new crash appears, begin from fresh runtime evidence after this fix

## 2026-04-09 Host protocol drift and RGB watermark decode cleanup

Another real issue was found while reading the new stack logs:

- firmware protocol source is `config/proto/usb_comm.proto`
- host proto generation had still been reading stale `deps/zmkx.app/proto/comm.proto`
- firmware had event IDs:
  - `110 = LOG_EVENT_USB_STACK_WATERMARK`
  - `111 = LOG_EVENT_RGB_WORKQ_STACK_WATERMARK`
- stale host bindings stopped at `100`
- result: valid watermark events rendered as generic
  - `Binary event 110 data0=... data1=...`

Current fix:

- `deps/zmkx.app/build-proto.mjs` now generates host bindings directly from:
  - `../../config/proto/usb_comm.proto`
- a compatibility export alias keeps existing frontend imports using `UsbComm`
- `deps/zmkx.app/src/stores/debug_decode.ts` also keeps a numeric fallback for `110/111`

RGB watermark decode also had a second drift:

- two RGB event packing variants existed across local diagnostic builds
- host had only decoded the newer one
- that produced misleading text like:
  - `0% used watermark at stage 6`

Current fix:

- `debug_decode.ts` now supports both RGB watermark layouts
- old-format RGB events are still rendered as readable watermark messages instead of fake
  `0%` / bogus stage output

Current practical meaning:

- USB crash root cause is fixed in firmware
- host protocol/decode drift for watermark events is fixed
- the RGB watermark logs seen so far are not crash-class evidence
## 2026-04-10 TouchBar multi-segment support completed; keyboard live-sync still missing

Latest completed work split into two different outcomes:

### TouchBar

Root cause that was fixed:

- TouchBar had been hard-coded to a left/right two-segment model through firmware, protocol,
  handler, and host rendering

What is now true:

- firmware supports `touchbar_segment_count`
- TouchBar config view exports/imports segment mask arrays up to 6 segments
- protocol now carries repeated segment masks
- legacy left/right fields remain for compatibility
- host TouchBar page supports add/remove segment
- strip preview renders continuous rounded bands from the actual masks instead of a permanent
  left/right assumption

### Keyboard page

Do not say it can sync live to the keyboard.

Verified root cause:

- no runtime keymap action exists in `config/proto/usb_comm.proto`
- no current firmware handler exists for runtime keymap edits on the active board target
- page edits `.keymap` source only

Current correct UX:

- edit draft bindings/layers/macros
- save/copy/export `.keymap`
- explicitly warn that live keymap sync over USB is not currently supported

### Build state

- host build passed
- firmware build passed
- current artifact:
  - `build/keyboard12/zephyr/zmk.uf2`
- latest report:
  - `FLASH 76088 B / 104 KB = 71.45%`
  - `SRAM 20168 B / 20 KB = 98.48%`

### Next-window reminder

- TouchBar multi-segment support is implemented for real
- keyboard live-sync would require a new firmware feature, not just another button
- memory is very tight now, so treat any new runtime config feature as a RAM-budgeted design task
