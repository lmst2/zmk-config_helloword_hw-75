# HW-75 ZMK Migration Context

## Purpose

This document captures the full design context for migrating:

- custom keyboard RGB effects from the original STM32 firmware
- custom TouchBar behavior from the original STM32 firmware

into the current ZMK-based project in:

- `e:\code\zmk-config_helloword_hw-75`

It is written so a new chat/window can continue implementation without losing architectural context.

No code has been written yet for this migration at the time of this document.

## Repositories and Roles

### Current working repo

Path:

- `e:\code\zmk-config_helloword_hw-75`

This is not a plain keymap-only repo. It is a Zephyr module/config repo with:

- board definitions
- DTS bindings
- drivers
- app modules
- protobuf definitions
- USB communication handlers

Important files:

- `config/west.yml`
- `config/zephyr/module.yml`
- `config/proto/usb_comm.proto`
- `config/app/usb_comm/handler/handler_rgb.c`
- `config/drivers/kscan/kscan_gpio_74hc165.c`
- `config/boards/arm/hw75_keyboard/*`
- `config/boards/arm/hw75_dynamic/*`

### Upstream used by build

Build manifest points at:

- repo: `https://github.com/xingrz/zmk.git`
- revision: `zmkx-rel-20231018`

Source:

- `config/west.yml`

Meaning:

- To do RGB properly, we likely need to modify both this config/module repo and the `xingrz/zmk` fork.
- Especially true if we want custom effects to be part of the normal ZMK underglow effect pipeline.

### Original firmware repo

Path:

- `e:\code\HelloWord-Keyboard`

Relevant original firmware path:

- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw`

Relevant hardware docs path:

- `e:\code\HelloWord-Keyboard\1.Hardware`
- `e:\code\HelloWord-Keyboard\5.Docs`

## Current RGB Architecture in ZMK Repo

### Shared protocol and handler path

RGB on both boards currently goes through:

- `config/proto/usb_comm.proto`
- `config/app/usb_comm/handler/handler_rgb.c`

Current protocol:

- `RgbControl` has commands like `RGB_EFF`, `RGB_EFR`, hue/sat/bri/speed controls.
- `RgbState.Effect` currently only has 4 enums:
  - `SOLID = 0`
  - `BREATHE = 1`
  - `SPECTRUM = 2`
  - `SWIRL = 3`

Current handler behavior:

- does not render anything custom itself
- delegates entirely to ZMK underglow APIs:
  - `zmk_rgb_underglow_on()`
  - `zmk_rgb_underglow_off()`
  - `zmk_rgb_underglow_change_*()`
  - `zmk_rgb_underglow_cycle_effect()`
  - `zmk_rgb_underglow_select_effect()`
  - `zmk_rgb_underglow_calc_effect()`

Conclusion:

- The current RGB system is a thin management/protocol layer over ZMK underglow.
- If we want custom effects to feel native, we should extend the actual underglow effect system, not bolt on a parallel renderer.

## Current Board RGB Topologies

### `hw75_dynamic`

File:

- `config/boards/arm/hw75_dynamic/hw75_dynamic.dts`

Facts:

- `zmk,underglow = &led_strip`
- `led_strip` chain length is `4`
- only a small 4-LED module exists here

This is not a full keyboard-per-key RGB surface.

Also:

- `CONFIG_ZMK_RGB_UNDERGLOW_EFF_START=2`
- from `config/boards/arm/hw75_dynamic/hw75_dynamic_defconfig`

Meaning:

- `dynamic` already starts on a different effect than keyboard.
- Current coexistence is one shared RGB API/protocol, but different board topology and some different defaults.

### `hw75_keyboard`

Base file:

- `config/boards/arm/hw75_keyboard/hw75_keyboard.dts`

Facts:

- base `led_strip` chain length is `101`

Board 1.2 overlay:

- `config/boards/arm/hw75_keyboard/hw75_keyboard_1_2_0.overlay`

Facts:

- overrides `led_strip.chain-length = 103`
- overrides `ws2812.chain-length = 103`

Meaning:

- keyboard 1.2 should be treated as 103 LEDs in ZMK
- not 101
- not 104

## Original Firmware RGB Architecture

Relevant files:

- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\HelloWord\hw_keyboard.h`
- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\HelloWord\hw_keyboard.cpp`
- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\UserApp\main.cpp`

Key facts:

- `LED_NUMBER = 104`
- `KEY_NUMBER = 82`
- `TOUCHPAD_NUMBER = 6`

Custom light effects implemented in original firmware:

- `EFFECT_RAINBOW_SWEEP`
- `EFFECT_REACTIVE`
- `EFFECT_AURORA`
- `EFFECT_RIPPLE`
- `EFFECT_STATIC`

Original render loop is in:

- `UserApp/main.cpp`

Original effect implementation structure:

- a single render function `RenderLightEffect()`
- one effect chosen from `keyboard.currentEffect`
- custom LED coordinate mapping from `getLedPos()`
- effect-specific state stored in static locals

Original keyboard-side RGB also supports:

- per-key press intensity accumulation via `UpdateKeyPressState()`
- special status LEDs
- sleep fade / pulse behavior

## Original RGB Geometry

From `getLedPos()` in original firmware:

- LEDs `0..81` are keyboard key LEDs
- LEDs `82..84` are status LEDs near arrow cluster
- LEDs `85..103` are the bottom/hub strip region

So original firmware count is:

- `82` key LEDs
- `3` status LEDs
- `19` hub/base LEDs
- total `104`

## 103 vs 104 Decision

### What is known

Original firmware:

- `104` LEDs total

Current ZMK keyboard 1.2:

- `103` LEDs total

Current ZMK keyboard 1.2 map still preserves:

- 82 key LEDs
- 3 status LEDs

Therefore the mismatch is in the hub/base region.

Most likely:

- original firmware hub/base region had 19 LEDs
- ZMK keyboard 1.2 layout currently models 18 LEDs in that region

### What is not yet fully proven

Not yet precisely identified:

- which exact physical hub/base LED index from the old 104-layout is missing or merged in the ZMK 103-layout

### Final design decision

Do not preserve the old `104`-LED geometry.

Instead:

- treat keyboard 1.2 as authoritative `103` LEDs
- rebuild LED coordinate tables for ZMK from the current DTS/overlay mapping
- optimize effects against the actual ZMK board topology

This is the correct direction because the target firmware should match the actual board layout used by ZMK, not force legacy assumptions.

## RGB Coexistence Design

### High-level decision

Keep one RGB protocol surface, but support board-specific effect capability sets and board-specific renderers.

### Why

`dynamic` and `keyboard` should not expose identical effect lists:

- `keyboard` is a large spatial lighting surface
- `dynamic` is a 4-LED module

Effects like:

- `Reactive`
- `Ripple`

make sense on keyboard but do not naturally map to `dynamic`.

### Final RGB design

1. Keep current shared USB comm entry points.
2. Extend the underlying RGB effect system so custom effects are part of the normal underglow cycle order.
3. Add capability reporting so host software can know which effects are supported on the current board.
4. Use board-specific effect registration:
   - keyboard: full custom effect set
   - dynamic: only small-LED-appropriate effects

### Target effect exposure

#### Keyboard

Use ZMK stock effects, then append keyboard-specific custom effects after them.

Intended custom keyboard effects:

- Rainbow Sweep
- Reactive
- Aurora
- Ripple
- Static

Possibly also migrate some of the original sleep/status ideas later, but not required for first pass.

#### Dynamic

Do not expose the same full list.

Recommended:

- keep stock 4 effects
- optionally add 1-2 dynamic-specific micro effects later
- do not expose keyboard-oriented `Reactive` and `Ripple`

## RGB Implementation Strategy

### Needed change split

#### In `xingrz/zmk` fork

Likely needed:

- extend underglow core effect enum/routing
- allow custom/vendor effect registration or board-conditional custom effect hooks
- make effect count/query accessible to callers

This is the cleanest way to make new effects:

- cycle naturally using existing `RGB_EFF` / `RGB_EFR`
- report naturally through `zmk_rgb_underglow_calc_effect()`
- persist naturally with existing underglow state mechanisms

#### In current config/module repo

Needed:

- board-specific custom effect implementations
- new LED coordinate table for keyboard 1.2 103-LED topology
- host protocol capability extensions
- protocol handler changes for new effect range/capability reporting

### Protocol changes recommended

Current `RgbState.Effect` fixed enum of 4 values is too rigid.

Recommended direction:

1. Keep existing effect IDs for current stock effects to avoid breaking current behavior.
2. Append new custom effect IDs after stock ones.
3. Add a capability message or capability fields, for example:
   - board type / board id
   - supported effect count
   - optional supported effect bitset or list

This avoids assuming `dynamic` and `keyboard` support the same list.

## Current TouchBar Situation in ZMK Repo

### Current wiring

`hw75_keyboard` uses:

- `zmk,kscan-gpio-74hc165`
- 11 chained 74HC165 chips

Files:

- `config/boards/arm/hw75_keyboard/hw75_keyboard.dts`
- `config/drivers/kscan/kscan_gpio_74hc165.c`

Current keyboard keymap includes:

- a `TOUCH` layer
- 6 touch positions currently represented in keymap

File:

- `config/hw75_keyboard.keymap`

Current behavior is essentially:

- treat touchbar pads as normal key inputs and map them through layer logic

### Why this is not enough

Original TouchBar is not six simple buttons.

It is a gesture surface with:

- segment selection
- averaged position
- press/release grace windows
- edge repeat logic
- app switch mode
- desktop switch mode
- pan mode

Current keymap-based use is much simpler and cannot reproduce original semantics.

## Original TouchBar Architecture

Relevant files:

- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\HelloWord\hw_keyboard.cpp`
- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\UserApp\main.cpp`

Key facts:

- TouchBar is 6 touch channels
- raw touch state is remapped into a logical left-to-right 6-bit state
- original firmware notes that the touch IC reports points in a non-linear physical order
- raw-to-logical mapping in original code:
  - `RAW_BIT_BY_LOGICAL_POSITION = {0, 5, 4, 3, 2, 1}`

TouchBar modes in original firmware:

- `TOUCHBAR_MODE_PAN`
- `TOUCHBAR_MODE_APP_SWITCH`
- `TOUCHBAR_MODE_DESKTOP_SWITCH`

Original mode switch shortcut:

- `Fn + RightCtrl`

Original mode status feedback:

- status LED blink count based on mode

### Original timing assumptions

Original TouchBar loop runs from:

- `OnTimerCallback()` at 1000Hz

Important original timing parameters:

- activation: `20ms`
- app activation: `90ms`
- release grace: `35ms`
- switch release grace: `90ms`
- pan interval: `12ms`
- app step interval: `55ms`
- desktop step interval: `500ms`

This logic expects continuous sampling, not sparse high-level button events.

## TouchBar Hardware Facts

From original repo `README.md` and hardware docs:

- TouchBar is an optional capacitive module
- built from a 6-channel touch IC `XW06A`
- connected back into the keyboard through the same scan system
- original README explicitly says TouchBar is also read through `74HC165`

This matters because it means:

- we do not need a separate second hardware owner for the TouchBar
- the existing 74HC165 path can remain the single source of truth
- but we do need access to raw state, not just debounced key events

## Why Not Just Adapt Original TouchBar Algorithm to Current Key Event Rate

This question was considered explicitly.

Conclusion:

- possible in theory
- not recommended as the primary design

### Reason

The original algorithm consumes continuous state snapshots.

The current ZMK key-processing path introduces:

- key debounce
- event discretization
- potential idle polling delay

Current 74HC165 defaults from binding:

- `debounce-press-ms = 5`
- `debounce-release-ms = 5`
- `debounce-scan-period-ms = 1`
- `poll-period-ms = 10`

This is excellent for regular keys, but not ideal for gesture reconstruction.

### Expected issues if TouchBar stays as normal key events

- higher first-touch latency
- fast swipes can lose intermediate states
- greater chance of feeling discontinuous
- release-order jitter becomes harder to reason about
- edge hold/repeat becomes less stable

### Important nuance

This is not because ZMK is too slow overall.

It is because:

- ordinary key event semantics are the wrong abstraction layer for a gesture surface

## Final TouchBar Design Decision

### Chosen design

Modify the existing `74hc165` driver interface to expose raw TouchBar 6-bit state, then build TouchBar as a separate app/module on top of that raw state.

### Explicitly chosen

- keep existing 74HC165 driver as the sole hardware reader
- add raw state access
- create a `touchbar_app`
- remove the 6 touch pads from normal ZMK key ownership for behavior purposes

### Explicitly not chosen

- do not keep TouchBar as normal keymap buttons and try to fully reconstruct gesture behavior from debounced key events
- do not create a second parallel SPI hardware owner that bypasses the existing scan driver entirely

### Why this is the best tradeoff

It preserves:

- one hardware reader
- one source of truth
- most of the original algorithm
- better gesture quality

while avoiding:

- fighting ZMK’s normal key event model
- driver ownership conflicts

## Planned TouchBar Architecture

### Driver-level changes

Target file:

- `config/drivers/kscan/kscan_gpio_74hc165.c`

Planned concept:

- after each scan, expose raw scan bitmap or a derived raw TouchBar 6-bit state
- make this available to a TouchBar app

Possible API shapes:

- function to fetch latest raw bytes
- function to fetch latest raw touchbar 6-bit value
- callback registration for raw scan state updates

Preferred pragmatic choice:

- expose a simple latest raw TouchBar state getter plus timestamp if needed

This is enough for first pass.

### TouchBar app

Planned new module, likely under current repo app area.

Responsibilities:

- reconstruct logical 6-bit TouchBar state from raw scan bytes
- apply original raw-bit reorder mapping
- port original session state machine
- emit HID keyboard/mouse actions as needed
- support persisted config
- support host-side config later

### Behavior design

Port original modes:

- Pan
- App Switch
- Desktop Switch

But do not keep original hardcoded `Fn + RightCtrl` as the only way to switch.

Recommended:

- create explicit TouchBar behavior/action for mode cycle
- optionally bind default key combo in keymap to preserve current UX

## HID / Action Emission Reuse

Current repo already has useful paths for synthetic HID actions:

- `config/app/hid_mouse.c`
- `config/app/behaviors/behavior_mouse_wheel.c`

These can likely be reused for:

- pan mode mouse-wheel output

For keyboard shortcut emission:

- TouchBar app will likely need to raise synthetic key presses or use ZMK behavior invocation paths

Exact implementation can be chosen during coding depending on what is simplest in this codebase.

## Settings / Host Config Design

### Existing patterns worth following

Indicator settings:

- `config/app/indicator.c`

Knob settings and host config:

- `config/boards/arm/hw75_dynamic/app/knob_app.c`
- `config/app/usb_comm/handler/handler_knob.c`

These show the desired style:

- persisted settings via `settings`
- USB comm get/set handlers
- capability advertisement in version/features

### TouchBar should follow this style

Recommended TouchBar host config surface:

- mode enable/default mode
- timing parameters if needed
- maybe per-mode sensitivity tuning later

Minimum first-pass config:

- enable/disable
- current mode
- maybe invert directions if user needs it

### Protocol plan

Add TouchBar feature advertisement to version/features.

Add TouchBar messages:

- `TOUCHBAR_GET_CONFIG`
- `TOUCHBAR_SET_CONFIG`

Potentially later:

- mode state report

## File Areas Likely to Change

### In current repo

Likely edits/additions:

- `config/proto/usb_comm.proto`
- `config/proto/CMakeLists.txt`
- `config/app/usb_comm/handler/handler_version.c`
- `config/app/usb_comm/handler/CMakeLists.txt`
- `config/app/usb_comm/handler/handler_rgb.c`
- `config/drivers/kscan/kscan_gpio_74hc165.c`
- new TouchBar app sources under `config/app/` or board app area
- related `Kconfig` and `CMakeLists.txt`
- `config/hw75_keyboard.keymap`
- maybe board Kconfig selections

Potential new files:

- TouchBar app source/header
- TouchBar USB comm handler
- TouchBar settings definitions
- RGB custom effect sources
- keyboard 1.2 LED coordinate table
- optional dynamic-specific RGB effect source

### In `xingrz/zmk` fork

Likely edits:

- RGB underglow core effect enum
- effect cycling logic
- effect rendering dispatch
- state/query helpers

Need to inspect actual fork source in next implementation session.

## Recommended Implementation Order

### Phase 1: RGB foundations

1. Inspect `xingrz/zmk` underglow internals.
2. Add effect extensibility or append custom effects directly in fork.
3. Extend protocol for effect capability awareness.
4. Implement keyboard 1.2 custom effect rendering against 103-LED geometry.
5. Wire effect selection/cycling naturally through existing RGB controls.

### Phase 2: Dynamic RGB

1. Decide dynamic-specific supported effect list.
2. Add capability reporting so host can distinguish keyboard vs dynamic.
3. Add any dynamic-only mini effects if desired.

### Phase 3: TouchBar raw path

1. Extend 74HC165 driver with raw state exposure.
2. Build TouchBar app consuming raw 6-bit state.
3. Port original state machine.
4. Disable ordinary key ownership/use of the 6 touch pads in keymap logic.

### Phase 4: TouchBar host config

1. Add TouchBar capability bit in version/features.
2. Add TouchBar get/set config messages.
3. Persist settings via `settings`.

### Phase 5: polish

1. Fine-tune gesture timing in real hardware testing.
2. Verify keyboard 1.2 LED topology visually.
3. Verify host software behavior.

## Design Principles Agreed

1. Prefer native integration over side-channel hacks.
2. RGB should feel like an extension of existing ZMK underglow, not a parallel subsystem.
3. `dynamic` and `keyboard` should not be forced into an identical effect list.
4. TouchBar should use raw state, not ordinary debounced key events.
5. Do not create two competing hardware readers for the same scan chain.
6. Reuse existing patterns in the repo:
   - `settings`
   - USB comm handlers
   - feature advertisement
7. Optimize for natural host UX:
   - host should know actual supported features/effects
   - keyboard and dynamic should present capabilities honestly

## Important Open Questions

These are still open and should be resolved during implementation:

1. Exact internal structure of RGB underglow in `xingrz/zmk` fork.
2. Best minimal API shape for exporting raw touchbar state from `74hc165`.
3. Exact cleanest path for synthetic keyboard shortcut emission from TouchBar app inside ZMK.
4. Exact visual mapping of the missing `104 -> 103` hub LED difference on hardware 1.2.

These open questions do not block coding start, but they do affect implementation details.

## Things Already Decided and Should Not Be Re-litigated Unless Blocked

1. TouchBar will use raw 6-bit state from driver interface changes.
2. TouchBar will become an app/module, not stay as 6 normal ZMK keys.
3. RGB custom effects should integrate with the normal underglow effect flow.
4. Keyboard and dynamic will have board-appropriate effect sets.
5. Keyboard target geometry is 103 LEDs for board 1.2.

## Useful Source Paths

### Current repo

- `e:\code\zmk-config_helloword_hw-75\config\west.yml`
- `e:\code\zmk-config_helloword_hw-75\config\zephyr\module.yml`
- `e:\code\zmk-config_helloword_hw-75\config\proto\usb_comm.proto`
- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\handler\handler_rgb.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\usb_comm\handler\handler_version.c`
- `e:\code\zmk-config_helloword_hw-75\config\drivers\kscan\kscan_gpio_74hc165.c`
- `e:\code\zmk-config_helloword_hw-75\config\dts\bindings\kscan\zmk,kscan-gpio-74hc165.yaml`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_keyboard\hw75_keyboard.dts`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_keyboard\hw75_keyboard_1_2_0.overlay`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_keyboard\hw75_keyboard_defconfig`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_keyboard\Kconfig.usb`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_dynamic\hw75_dynamic.dts`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_dynamic\hw75_dynamic_defconfig`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_dynamic\Kconfig.usb`
- `e:\code\zmk-config_helloword_hw-75\config\app\indicator.c`
- `e:\code\zmk-config_helloword_hw-75\config\boards\arm\hw75_dynamic\app\knob_app.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\hid_mouse.c`
- `e:\code\zmk-config_helloword_hw-75\config\app\behaviors\behavior_mouse_wheel.c`
- `e:\code\zmk-config_helloword_hw-75\config\hw75_keyboard.keymap`

### Original firmware

- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\HelloWord\hw_keyboard.h`
- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\HelloWord\hw_keyboard.cpp`
- `e:\code\HelloWord-Keyboard\2.Firmware\HelloWord-Keyboard-fw\UserApp\main.cpp`

### Original hardware/docs

- `e:\code\HelloWord-Keyboard\README.md`
- `e:\code\HelloWord-Keyboard\1.Hardware\SCH_HelloWord-Keyboard_2022-07-31.pdf`
- `e:\code\HelloWord-Keyboard\1.Hardware\SCH_HelloWord-TouchBar_2022-07-31.pdf`
- `e:\code\HelloWord-Keyboard\5.Docs\1.Datasheet\C389301_XW06A_2019-04-25.PDF`

## Immediate Next Step for New Chat

When continuing implementation in a new window:

1. Read this file first.
2. Inspect the `xingrz/zmk` underglow source to determine exact RGB extension points.
3. Start with RGB architecture changes, not TouchBar.
4. After RGB foundation is clear, implement TouchBar raw-state path.

## Final Summary

The agreed final design is:

- RGB:
  - native underglow integration
  - board-specific supported effect sets
  - keyboard 1.2 optimized for 103 LEDs
  - dynamic optimized separately for its 4 LEDs
  - protocol extended for capability awareness

- TouchBar:
  - not treated as ordinary ZMK key events
  - raw 6-bit state added at driver interface level
  - original gesture logic ported into a dedicated TouchBar app
  - host config added later following existing indicator/knob patterns

This is the intended direction and should be used as the baseline for coding.
