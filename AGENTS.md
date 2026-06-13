# HW-75 Agent 工作指南

> 本文档是 **agent 接入项目的唯一必读文件**。每次接到改动需求，先读完它，再动手。目的：让 agent 快速理解代码状态、准确定位改动点、避免遗漏副作用、避免重复造轮子。
>
> - 迁移历史与决策记录见：`MIGRATION_CONTEXT.md`、`MIGRATION_HANDOFF.md`、`MIGRATION_HANDOFF_NEXT.md`（**只读参考**，不是当前状态的权威来源）。
> - 当前状态以**源码为准**，迁移文档仅供溯源。

---

## 1. 项目定位（一句话）

这是 HelloWord HW-75 模块化机械键盘的 **ZMK 固件 + 上位机配置应用**的一体化仓库，同时承担：

- Zephyr **module/config 仓**（板子定义、驱动、app 代码、protobuf）
- **vendored ZMK 分支**（`deps/zmk`，本地魔改过）
- **vendored 上位机 Vue 应用**（`deps/zmkx.app`）
- **Windows 端本地 helper 服务**（`tools/hw75-helper`）
- **GitHub Actions CI** 基于 vendored 的 `deps/zmk` 编译，不再外拉 `xingrz/zmk`

两款硬件目标：

| Board                   | 芯片         | 角色                    | 特有子系统                                    |
| ----------------------- | ------------ | ----------------------- | --------------------------------------------- |
| `hw75_keyboard@1.1/1.2` | STM32F103XB  | 主键盘 (82 键 + TouchBar) | kscan 74HC165、103 LED 灯带、TouchBar、Function Slot |
| `hw75_dynamic@A/B`      | STM32F405XG  | 可拆扩展模块 (旋钮+墨水屏) | knob、SSD1306 OLED、SSD1675 e-ink、4 LED         |

两款板子**共享 usb_comm 协议、RGB 协议入口、diag_log 体系**，但功能集不同（由 `Kconfig.usb` 中 `select HW75_USB_COMM_FEATURE_*` 决定），并在协议 `Version.Features` 中上报。

**本仓库 owner 的实机**：`hw75_keyboard@1.2` + `hw75_dynamic@B`。本地编译、刷写、线上测试默认按这个组合走；其他 revision（`@1.1` / `@A`）只需要在 CI 矩阵里继续保证编得过，不是日常开发目标。

---

## 1.1 产品架构演进目标（**所有新功能先读这段**）

当前仓库里 `tools/hw75-helper` 在产品语义上是"上位机 core"（下一步会改名），`deps/zmkx.app` 是"上位机 UI"。两者关系已规划迁移到新链路：

- **目标数据流**：`keyboard ⇄ helper-core ⇄ webapp`（helper 居中），而非历史的 `keyboard ⇄ webapp ⇄ helper`。
- **helper-core 职责**：用 `node-hid` 直连键盘并维持唯一 USB 会话；持有业务状态；跑定时任务（天气 10min、时钟 1min 等）；主动向键盘推数据；订阅键盘异步事件；对网页提供 WebSocket（`ws://127.0.0.1:8755/ws`）代理 H2D/D2H 消息并广播事件。
- **webapp 职责**：仅作为可视化 UI，通过 WebSocket 代理调用 helper-core 的能力；**不再承担长期运行的业务逻辑**（用户会关掉浏览器）。
- **迁移边界（当前状态）**：
  - 新功能（eink 多模式、时钟天气、knob 零点校准、以后的 CPU 使用率等实时数据）**必须走 helper-core 新链路**（`deps/zmkx.app/src/stores/helperCore.ts` 的 `sendViaCore` / `onEvent`）
  - 旧功能（rgb、knob_prefs、touchbar、function_slots、debug_log）**暂保持 WebHID 直连**（`stores/usb.ts::send`），下次专门任务再迁移，避免爆炸半径失控
  - 两套链路短期共存，靠 URL / feature 开关区分使用场景
- **协议仍是单一源**：helper-core 和 webapp 都复用 `deps/zmkx.app/src/proto/comm.proto.{js,d.ts}` 生成产物，固件继续吃 `config/proto/usb_comm.proto` 生成的 nanopb。**所有跨端改动仍按 4.1 / 4.2 节的同步表走**，只是多了一端（helper-core）需要一起改。
- **命名约定**：涉及新链路的文件统一用 `core` 前缀（`helperCore.ts`、`coreConfig.json` 等），便于未来整体改名。

老迁移文档 `MIGRATION_*.md` 里没有这一段，它是 2026 年后规划，后续 agent 新建功能时必须以此节为准。

---

## 2. 目录速查（"这个东西在哪"）

```
├── config/                          ★ Zephyr module 根（ZMK_CONFIG 指向这里）
│   ├── CMakeLists.txt / Kconfig     聚合 app/drivers/proto 三个子树
│   ├── zephyr/module.yml            把 config/ 注册成 Zephyr module
│   ├── west.yml                     仅本地 west 初始化用（CI 用根 west.yml）
│   ├── hw75_keyboard.keymap         键盘 keymap (BASE/FN/TOUCH 三层)
│   ├── hw75_dynamic.keymap          dynamic keymap (sensor-binding 风格)
│   │
│   ├── proto/
│   │   ├── usb_comm.proto           ★★★ 通信协议唯一源（固件+上位机共用）
│   │   ├── uart_comm.proto          keyboard → dynamic SLIP 联动协议（独立于 usb_comm）
│   │   ├── usb_comm.keyboard.options ★ per-board nanopb 选项：FT_IGNORE dynamic-only + FT_CALLBACK 大 payload
│   │   └── usb_comm.dynamic.options  ★ per-board nanopb 选项：FT_IGNORE keyboard-only
│   │
│   ├── app/                         ★ 板无关的 app 层（启用与否由 Kconfig）
│   │   ├── Kconfig / CMakeLists.txt
│   │   ├── diag_log.c/.h            结构化二进制日志（事件+快照+boot events）
│   │   ├── touchbar.c               TouchBar 6 通道手势状态机（Pan/App/Desktop）
│   │   ├── function_slot.c          5 个可配置的功能位（HID preset/组合键/宏/helper）
│   │   ├── rgb_effects.c            键盘自定义 RGB 效果（接入 ZMK underglow 的 __weak hook）
│   │   ├── indicator.c              RGB 指示灯条（status LED 区段）
│   │   ├── hid_mouse.c              HID_2 鼠标设备（TouchBar pan/wheel 专用）
│   │   ├── storage_init.c           NVS 初始化（被 indicator/function_slot/touchbar 依赖）
│   │   │
│   │   ├── include/app/             ★ 公共头；跨文件调用请走这里
│   │   │   ├── diag_log.h           枚举 hw75_diag_* 必须与 proto LogEvent/Module/Level 同步
│   │   │   ├── touchbar.h           hw75_touchbar_config_view 等
│   │   │   ├── function_slot.h      slot/macro/caps/event 类型
│   │   │   ├── hw75_rgb_effects.h   HW75_RGB_EFFECT_* 枚举，和 proto RgbState.Effect 同步
│   │   │   ├── indicator.h          indicator_set_* 及 settings 结构
│   │   │   ├── hid_mouse.h          hid_mouse_wheel_report 等
│   │   │   └── kscan_74hc165.h      hw75_kscan_74hc165_get_touchbar_state/map
│   │   │
│   │   ├── behaviors/               DTS 绑定 behavior 实现
│   │   │   ├── behavior_touchbar_mode.c      &tb_mode
│   │   │   ├── behavior_eink_mode_cycle.c    &ekcyc（dynamic 墨水屏模式循环，CONFIG_HW75_EINK_MODES）
│   │   │   ├── behavior_function_slot.c      &fn_slot <idx>
│   │   │   ├── behavior_mouse_wheel.c        &mwh（鼠标滚轮）
│   │   │   └── behavior_lvgl_key_press.c     &lvkp（dynamic 屏幕用）
│   │   │
│   │   └── usb_comm/                ★ 协议分发框架
│   │       ├── usb_comm_hid.c       单 HID TX 缓冲+信号量（单 slot 发送模型）
│   │       ├── usb_comm_proto.c     RX packet 组装 + handler 分发
│   │       ├── usb_comm_handler.ld  handler 注册 section（iterable 遍历）
│   │       └── handler/             每个 Action 一个 handler 文件
│   │           ├── handler.h                 USB_COMM_HANDLER_DEFINE 宏
│   │           ├── handler_version.c         VERSION + Features 上报
│   │           ├── handler_rgb.c             RGB_CONTROL/GET/SET + INDICATOR
│   │           ├── handler_eink.c            EINK_SET_IMAGE（仅 dynamic）
│   │           ├── handler_eink_mode.c       EINK_GET/SET_CONFIG/SET_ACTIVE/PUSH_*（仅 dynamic，modes 走 callback）
│   │           ├── handler_knob.c            KNOB_*（仅 dynamic，含 GET/SET_CALIBRATION）
│   │           ├── handler_debug_log.c       LOG_GET_STATE/EVENTS/SET_CONFIG/CLEAR
│   │           ├── handler_touchbar.c        TOUCHBAR_GET/SET_CONFIG（仅 keyboard，嵌套子 Config + masks 全 FT_CALLBACK）
│   │           └── handler_function_slot.c   FUNCTION_SLOT_*（仅 keyboard，repeated 字段全 FT_CALLBACK）
│   │
│   ├── drivers/                     ★ 自定义 Zephyr driver
│   │   ├── kscan/kscan_gpio_74hc165.c       键盘矩阵 + TouchBar 原始态导出
│   │   ├── led_strip/led_strip_remap.c      逻辑→物理 LED 重映射 + 区段标签
│   │   ├── display/{ssd16xx,display_sw_rotate}.c  eink / 旋转屏
│   │   ├── console/uart_slip.c              keyboard↔dynamic UART SLIP
│   │   └── sensor/knob/                     旋钮 profile 驱动
│   │
│   ├── dts/                         ★ DTS 绑定和片段
│   │   ├── bindings/{behaviors,display,kscan,led_strip,sensor}/
│   │   ├── behaviors/*.dtsi         keymap 用的 /omit-if-no-ref/ 片段
│   │   └── dt-bindings/zmk/{lvgl,mouse}.h
│   │
│   ├── cmake/                       构建脚手架
│   │   ├── version.cmake            生成 VER_APP（含 git hash）
│   │   ├── lv_font_conv.cmake       LVGL 字体子集
│   │   ├── font_subset.cmake
│   │   └── ui_strings.cmake / ui_strings.py  dynamic 屏幕字符串打表
│   │
│   └── boards/arm/
│       ├── hw75_keyboard/           STM32F103 板文件
│       │   ├── hw75_keyboard.dts / _1_1_0.overlay / _1_2_0.overlay
│       │   │   ※ 1.2 把 led_strip 改成 103，1.1 保持 101
│       │   ├── hw75_keyboard_defconfig   CONFIG_USB_HID_DEVICE_COUNT=3 等
│       │   ├── Kconfig.{usb,rgb,uf2,rtt,defconfig,board}
│       │   └── app/uart_comm/                      UART SLIP 报告层（keyboard 侧）
│       └── hw75_dynamic/            STM32F405 板文件
│           ├── hw75_dynamic.dts / _A.overlay / _B.overlay
│           └── app/
│               ├── knob_app.c/.h                  旋钮交互 + profile 切换
│               ├── eink_app.c/.h                  e-ink 渲染
│               ├── indicator_app.c                指示条
│               ├── screen/                        LVGL 状态屏（status_screen/knob_status/knob_indicator/layer_status）
│               ├── uart_comm/                     对端 SLIP handler
│               └── include/app/events/            knob_state_changed / eink_state_changed
│
├── deps/                            ★ vendored 子项目（**当作本仓库代码**维护）
│   ├── zmk/                         魔改 ZMK，CI/本地 build 都用这里
│   │   └── app/src/rgb_underglow.c  ★ 已加 __weak hook：
│   │       zmk_rgb_underglow_custom_effect_{count,mask,render}
│   └── zmkx.app/                    Vue3 + Pinia + ant-design-vue 上位机
│       ├── package.json / vite.config.js
│       ├── build-proto.mjs          ★ 直接读 config/proto/usb_comm.proto 生成 TS
│       ├── build-keyboard-config.mjs  Keyboard 页面键位表生成
│       ├── src/proto/comm.proto.{js,d.ts}  ← 构建产物，别手改
│       ├── src/generated/keyboard-config.ts  ← 构建产物
│       ├── src/utils/usb/
│       │   ├── usb.ts                   IUsbCommDevice/IUsbCommTransport 接口 + trace 模型
│       │   └── usb-hid.ts               WebHID 实现（分包、重组、trace 生成）
│       ├── src/stores/              Pinia 各域状态（每个对应一个协议子集）
│       │   ├── usb.ts                   ★ 全局请求串行化（requestQueue + activePendingRequest）
│       │   ├── version.ts / rgb.ts / knob.ts / eink.ts
│       │   ├── touchbar.ts / function-slots.ts
│       │   ├── debug.ts                 LOG_GET_STATE/EVENTS 轮询
│       │   └── debug_decode.ts          原始事件→人读字符串
│       ├── src/routes/              每个侧栏对应一个 Vue 页面
│       │   ├── About.vue / Keyboard.vue / Rgb.vue / Eink.vue
│       │   ├── Motor.vue / MotorDemo.vue / MotorPrefs.vue
│       │   ├── Touchbar.vue / Debug.vue
│       ├── src/pages/Main.vue           ★ 侧栏可见性由 Version.Features 决定
│       └── src/utils/helper-bridge.ts   与 hw75-helper HTTP API 的类型契约
│
├── tools/hw75-helper/               ★ Windows 本地 helper（浏览器上位机→本机动作桥）
│   └── src/server.mjs               127.0.0.1:8755 HTTP，动作表在顶部 ACTIONS
│
├── .github/workflows/build.yml      ★ CI：矩阵 4 板 + host app + release
├── west.yml                         根 west manifest（拉 zephyr，zmk 用本地 deps/zmk）
├── start-hw75-dev.{cmd,ps1}         Windows 一键起 helper + vite dev
├── MIGRATION_*.md                   历史决策与 handoff（仅供追溯）
└── AGENTS.md                        ← 本文件
```

---

## 3. 构建系统（**改前必懂**）

### 3.1 固件本地编译（Windows，当前主开发环境）

owner 日常只刷 `hw75_keyboard@1.2` 和 `hw75_dynamic@B`，两条命令：

```powershell
# PATH 里至少要有 protoc / dtc / ninja / cmake / python 3.12

# keyboard 1.2
py -3.12 -m west build -p always -s deps/zmk/app -d build\keyboard12 `
    -b hw75_keyboard@1.2 `
    -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" `
       "-DKEYMAP_FILE=E:/code/zmk-config_helloword_hw-75/config/hw75_keyboard.keymap"
# 产物：build/keyboard12/zephyr/zmk.uf2

# dynamic B
py -3.12 -m west build -p always -s deps/zmk/app -d build\dynamicB `
    -b hw75_dynamic@B `
    -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" `
       "-DKEYMAP_FILE=E:/code/zmk-config_helloword_hw-75/config/hw75_dynamic.keymap"
# 产物：build/dynamicB/zephyr/zmk.uf2
```

其他 revision（`@1.1` / `@A`）只在需要验证 CI 的时候按同样模板换 `-b` 和 `-d`。

### 3.2 固件 CI 编译

见 `.github/workflows/build.yml`：

- 矩阵 4 目标；源码根是 `deps/zmk/app`；`ZMK_CONFIG` 指到本仓库的 `config/`。
- protobuf python **必须锁 `4.25.3`**（nanopb 0.4.x plugin 需要 `<5`）。
- 产物命名：`firmware-<board>-zmk.uf2` / `.hex` / `.bin`。

### 3.3 上位机编译

```powershell
cd deps/zmkx.app
npm ci                 # postinstall 会自动跑 build:proto + build:keyboard
npm run dev            # vite dev：http://localhost:8080
npm run build          # 输出 deps/zmkx.app/dist
```

- **改了 `config/proto/usb_comm.proto` 必须 `npm run build:proto`**，否则上位机枚举漂移。
- **改了 `config/hw75_keyboard.keymap` 涉及 Keyboard 页默认值时，要 `npm run build:keyboard`**。

### 3.4 开发一键起

`start-hw75-dev.ps1`：起 `hw75-helper`（8755）和 vite dev（8080），有去重 +健康检查。

---

## 4. 协议与数据流（**所有跨端改动的总开关**）

### 4.1 协议源：`config/proto/usb_comm.proto`

- 包名 `usb.comm`，固件用 nanopb（生成 `usb_comm.pb.{h,c}`），上位机 JS/TS 直接读这份 `.proto`。
- 顶层消息：`MessageH2D`（host→device）、`MessageD2H`（device→host），都是 `action + oneof payload` 结构。
- 改任何一个枚举、字段、oneof，都**同时影响**：
  1. `config/app/usb_comm/handler/handler_*.c`（固件 handler）
  2. `deps/zmkx.app/src/proto/comm.proto.*`（`npm run build:proto` 重新生成）
  3. `deps/zmkx.app/src/stores/*.ts`（请求构造 + 响应解析）
  4. `deps/zmkx.app/src/stores/debug_decode.ts`（LogEvent / 枚举漂移兜底）
  5. `deps/zmkx.app/src/routes/*.vue`（UI 渲染）
  6. 如果是 feature 开关，还要在 `handler_version.c` 里正确置位 `Version.Features.*`。

### 4.2 Action/枚举 同步表（不同步必然出 bug）

| 固件侧 (`app/diag_log.h`)            | proto (`usb_comm.proto`)                 | 上位机 (`debug_decode.ts`)     |
| ------------------------------------ | ---------------------------------------- | ------------------------------ |
| `hw75_diag_level`                    | `LogLevel`                               | `levelText`                    |
| `hw75_diag_module`                   | `LogModule`                              | `moduleText` / `moduleColor`   |
| `hw75_diag_event_id`                 | `LogEventId`                             | `decodeEvent` 的 switch        |
| `hw75_rgb_effect_id`                 | `RgbState.Effect`                        | `Rgb.vue` 的 effect label      |
| `hw75_touchbar_mode`                 | `TouchbarMode`                           | `Touchbar.vue` mode 选择       |
| `hw75_function_slot_type` / `_preset`| `FunctionSlotType` / `supported_hid_presets` | `function-slots.ts`        |

**规则**：任何一侧加了新值，另外两侧必须一起加；老 ID 永不复用（见 MIGRATION，RgbState.Effect 4→9 就是 append）。

### 4.3 传输层关键约束（**写通信代码的红线**）

| 约束                                     | 地点                                                    | 违反后果                  |
| ---------------------------------------- | ------------------------------------------------------- | ------------------------- |
| **单 RX 槽**：`usb_rx_buf`+sem(max 1)    | `usb_comm_proto.c`                                      | 请求覆盖/报错漂移         |
| **上位机必须全局串行化请求**             | `stores/usb.ts` 的 `requestQueue + activePendingRequest`| 固件行为不定              |
| **TX 共享 `tx_buf`：先等 sem 再填**       | `usb_comm_hid.c::usb_comm_hid_send()`                   | 多包响应乱码              |
| **`MessageH2D/D2H` 不能放栈**            | `usb_comm_proto.c` 的 `static usb_h2d_msg/usb_d2h_msg`  | 1 KB 线程栈溢出、键盘死机 |
| **callback 编码必须读稳定拷贝**          | `handler_debug_log.c` 的 `ctx.events[]/boot_events[]`   | 多包响应成 "invalid wire type" |
| **callback 字段仅在有数据时 attach**     | 同上，`snapshots`/`boot_events`                         | 空 callback 扰乱下一个 callback |
| **repeated 字段不要同时 static+callback**| `.proto` 里 `LogEvents.events` 故意没有 `max_count`     | D2H 结构膨胀 → 栈溢出     |
| **新增 feature 要在 Version.Features 上报** | `handler_version.c`                                  | 上位机侧栏不显示          |
| **对方板不处理的 oneof 字段必须 FT_IGNORE** | `config/proto/usb_comm.{keyboard,dynamic}.options`    | 对方加 payload 会拖累本板 union size |
| **keyboard 大 payload 走 FT_CALLBACK**   | 同上；`handler_touchbar.c`/`handler_function_slot.c` 写 encode/decode | keyboard 20KB SRAM 吃不下 |

### 4.4 请求-响应全链路（以 RGB 为例）

```
┌──── Host (Vue) ──────┐              ┌──── Firmware ─────┐
Rgb.vue:toggle
  rgb.ts: send(action=RGB_CONTROL)
    stores/usb.ts: requestQueue.enqueue → comm.send → waitForResponse
      utils/usb/usb-hid.ts: 分包 + tx-trace                           
        hidDevice.sendReport ──── USB HID ───→ usb_comm_hid.c: RX cb
                                                 ↓ 组包写 usb_rx_buf
                                                 usb_comm_proto.c
                                                 ↓ pb_decode_delimited
                                                 STRUCT_SECTION_FOREACH 找 handler
                                                 handler_rgb.c: handle_rgb_control
                                                 ↓ zmk_rgb_underglow_*
                                                 ↓ 填 d2h->payload.rgb_state
                                                 pb_encode_delimited → usb_tx_buf
                                                 usb_comm_hid_send（等 sem→填→发）
  utils/usb/usb-hid.ts: RX 重组 → pb_decode ────── USB HID ──────────┘
    stores/usb.ts: handleTransferIn → rgbStore.$patch + resolve pending
      Rgb.vue: 响应式刷新
```

---

## 5. 核心子系统对照（**改之前先看这里**）

> 每个子系统都列出：对外入口（不要自己另写）、主要状态 owner、相关文件、跨系统依赖。

### 5.1 诊断日志 `diag_log`

- **只用 `hw75_diag_log_event(...)` 和 `hw75_diag_update_snapshot(...)`**，不要自己打 ring。
- event_id 新增：先在 `app/diag_log.h` 加 `HW75_DIAG_EVENT_*`，再在 `usb_comm.proto::LogEventId` 加同号，再在 `debug_decode.ts::decodeEvent` 写渲染逻辑。
- 上位机 Debug 页面通过 `stores/debug.ts` 按 `LOG_GET_STATE` + `LOG_GET_EVENTS`（批量 4）轮询。
- 事件环大小由 `CONFIG_HW75_DIAG_LOG_RING_SIZE`（键盘默认 8），boot events 由 `CONFIG_HW75_DIAG_LOG_BOOT_EVENT_COUNT`。**不要轻易调大**，SRAM 很紧。

### 5.2 USB 通信框架 `usb_comm`

- 新 Action 的三件套：
  1. `usb_comm.proto`：加 `Action.XXX`，给 H2D/D2H 加对应 `payload`。
  2. `config/app/usb_comm/handler/handler_xxx.c`：实现函数，顶部 `USB_COMM_HANDLER_DEFINE(Action_XXX, MessageD2H_payload_xxx_tag, handle_xxx)`。
  3. `config/app/usb_comm/handler/CMakeLists.txt`：按 Kconfig feature 条件 `zephyr_library_sources_ifdef`。
- handler 签名固定：`bool handle_xxx(const H2D*, D2H*, const void* bytes, uint32_t bytes_len)`。返回 `true` 才会设置响应 payload tag；返回 `false` 则响应变成空 Nop。
- `handler_version.c` 里 **必须**给新 feature 加 `features.xxx = true; has_xxx = true`，否则上位机侧栏不会出现。
- **per-board nanopb options**（`config/proto/usb_comm.{keyboard,dynamic}.options`，CMakeLists 按 `CONFIG_BOARD_*` 选一份 stage 到 build dir 为 `usb_comm.options`）：`.proto` 唯一源不变，但各板独立生成自己的 `usb_comm.pb.{h,c}`。两条用法：
  - **`type:FT_IGNORE`**：对方板的 oneof payload 从本板 union 里移除（encode/decode 层直接跳过，handler 根本不存在）。用来阻断跨板 proto 扩展的 SRAM 传染——dynamic 扩 eink_* 字段，keyboard 一个字节也不用付。
  - **`type:FT_CALLBACK`**：让单个字段（尤其是 `repeated`/嵌套 submessage）从"实体内嵌"退化成 8B `pb_callback_t`。encode 时 handler 主动挂 `funcs.encode`；decode 时由 `usb_comm_proto.c::h2d_callback` 的 submsg hook 挂 `funcs.decode`。参考 `handler_touchbar.c`（8 个子字段全 callback）和 `handler_function_slot.c`（3 个 repeated callback）。
- **新加 keyboard-only 大消息**（内嵌多个子 message / 大 `repeated`）：默认走 `FT_CALLBACK`，别走 `FT_STATIC`。看 §6 的 keyboard SRAM 水位。

### 5.3 RGB

- **入口分层**：
  1. `handler_rgb.c` 只做薄协议层，调 `zmk_rgb_underglow_*` API。不要在这里重写效果。
  2. 自定义效果通过 **ZMK vendored fork 的 `__weak` hook** 接入，实现在 `config/app/rgb_effects.c`：
     - `zmk_rgb_underglow_custom_effect_count()`
     - `zmk_rgb_underglow_custom_effects_mask()`
     - `zmk_rgb_underglow_custom_effect_render(ctx)`
  3. 效果 ID 枚举在 `app/hw75_rgb_effects.h::hw75_rgb_effect_id`，**必须**与 `usb_comm.proto::RgbState.Effect` 一一对应。
- **键盘几何**：`HW75_KEYBOARD_LED_COUNT=103`（hub 18 + keys 82 + status 3），写新效果就按这个布局；1.1 板是 101，两板的 overlay 中 `chain-length` 会覆盖。
- Indicator（status 3 灯）通过 `config/app/indicator.c` 独立管理，用 `led_strip_remap` 的 label lookup；**不要**直接写 status 灯。
- Dynamic 只有 4 灯，自定义效果不注册（见 `#if defined(CONFIG_BOARD_HW75_KEYBOARD)`）。

### 5.4 TouchBar

- 6 通道电容触摸，通过 kscan 74HC165 的 **row 10 / cols 4,1,3,5,0,2** 映射成逻辑 0..5（硬件实测序，**不要**再改）。
- 原始态导出 API（给 touchbar.c 用）：
  - `hw75_kscan_74hc165_get_touchbar_state(dev, *state, *ts)`
  - `hw75_kscan_74hc165_get_touchbar_logical_map(rows, cols)`
- 状态机实现在 `config/app/touchbar.c`（BOARD_HW75_KEYBOARD 保护），三种模式：`PAN`/`APP_SWITCH`/`DESKTOP_SWITCH`。
- 配置视图结构体：`struct hw75_touchbar_config_view`（`app/touchbar.h`）；与 `TouchbarConfig` proto 字段**一一映射**，通过 `touchbar_{get,set}_config_view()` 读写。
- Keymap 用 `&tb_mode`（`dts/behaviors/touchbar_mode.dtsi` + `behavior_touchbar_mode.c`）切模式，默认绑在 FN 层某位。
- 鼠标滚轮/Pan 通过 `hid_mouse.c` 的 `HID_2` 设备发出（**不要**动 `HID_0` 是键盘）。
- 上位机页面 `routes/Touchbar.vue` 通过 `TOUCHBAR_GET_CONFIG`/`TOUCHBAR_SET_CONFIG` 读写，支持 N 段（≤6）；改段数/掩码字段时，proto 和 `app/include/app/touchbar.h::hw75_touchbar_config_view` 要同步。

### 5.5 Function Slot

- 5 个位（`HW75_FUNCTION_SLOT_COUNT=5`），每位可配置为：HID preset / 组合键 / 宏（≤6步）/ helper 动作。
- 固件入口 `app/function_slot.h`；设置持久化走 `zephyr settings`（`hw75/fn_slot/...`）。
- Keymap 通过 `&fn_slot <index>`（`behavior_function_slot.c` + `dts/behaviors/function_slot.dtsi`）；每个 slot 支持 press/release 两个阶段。
- 上位机 `stores/function-slots.ts` 维护 cache；events 通过 `FUNCTION_SLOT_TRIGGER_EVENT_GET` 轮询（batch 4），helper 动作码会被 `helper-bridge.ts` 发到本机 `hw75-helper`。
- **新增 helper 动作**：同步改 `tools/hw75-helper/src/server.mjs::ACTIONS` 和 `deps/zmkx.app/src/utils/helper-bridge.ts::HelperActionCatalogItem` 列表。

### 5.6 Dynamic 专属

- knob：`boards/arm/hw75_dynamic/app/knob_app.c` + 8 种 profile（DTS 里定义）；host 通过 `KNOB_GET_CONFIG/SET_CONFIG/UPDATE_PREF`。
- eink：`eink_app.c` + `drivers/display/ssd16xx.c` + `drivers/display/display_sw_rotate.c`；host 通过 `EINK_SET_IMAGE`（走 `bytes` 字段，最大 8192）。
- OLED 状态屏：`app/screen/`（LVGL），字体子集由 `cmake/lv_font_conv.cmake` 在编译时生成。

### 5.7 上位机（`deps/zmkx.app`）

- 路由（`src/main.ts`）对应 `src/routes/*.vue`；**侧栏可见性统一由 `Version.Features` 驱动**（见 `Main.vue`）。加页面时，必须先在 Features 里加字段并让固件置位，否则别人连不上看不见。
- **禁止**在页面里直接调 `comm.send`；都走对应 `stores/*.ts`，确保串行化语义。
- 所有发送都会被 `stores/usb.ts::send` 排队，超时 1500ms。timeout 不是“失败”，是 `Promise.reject`，UI 要用 `try/catch` 处理。
- 国际化：Vue SFC 内嵌 `<i18n>`（zh-Hans/zh-Hant/en），键名留意 `t('...')` 调用。

---

## 6. 内存与资源约束（**当前非常紧**）

最近一次完整编译（2026-04，含 eink-modes / knob-calibration / uart_slip timeout / per-board options 瘦身）实测水位：

> ⚠️ **下表是手抄快照，不会自动更新**，可能已与 HEAD 漂移（实测过预编译 elf 的 SRAM 余量约 176 B 而非 296 B）。**永远以一次新构建为准**：`west build` 结尾的 Zephyr `Memory region … %age Used` 报告给出 SRAM/FLASH；`arm-zephyr-eabi-nm --size-sort build/<board>/zephyr/zmk.elf | findstr usb_.2._msg` 给出 oneof union 大小。CI 现已对 keyboard 加 SRAM 水位闸（`.github/workflows/build.yml`，超 20384 B 失败）。本仓库其它复述这些数字的地方（CLAUDE.md rule 3、§3、§8）都视为"约值，权威看本表 / 重编确认"。

| 资源 | keyboard@1.2 | dynamic@B | 说明 |
| --- | --- | --- | --- |
| SRAM | **98.55%**（20184 / 20480 B） | 58.41%（76560 / 131072 B，未用 CCM 64K） | keyboard margin ~296 B，靠 per-board options 把 dynamic-only payload `FT_IGNORE` 掉、把 TouchbarConfig/FunctionSlot* 改 `FT_CALLBACK` 才挤出来；再加任何 static buffer 前一定编一遍 |
| FLASH | 75.04%（79912 / 106496 B） | 56.56%（185328 / 327680 B） | keyboard 相对还有 25K；dynamic 比较宽松，`eink_frames_partition` 又单独占了 256K sector |
| `usb_h2d_msg` / `usb_d2h_msg` | 156 B / 144 B | 64 B / 76 B | oneof union 里 TouchbarConfig / FunctionSlot* 都走 callback；dynamic 更极端因为 keyboard-only 字段全 IGNORE 掉了 |
| `usb_comm` 线程栈 | 1024 B | 1024 B | H2D/D2H 已经挪到 `.bss`，别再塞大结构进栈 |
| `CONFIG_HW75_DIAG_LOG_RING_SIZE` | 8 | 8 | 加大会吃 SRAM |
| `MAX_BYTES_FIELD_SIZE`（proto bytes field） | 0（不启用） | 8192 | 给 eink frame / image 用 |

**红线**：
- **keyboard SRAM 仍然紧**：margin ~296 B。新加任何 static buffer / thread stack 前先本地编一遍看水位，超了立刻回退。额外瘦身候选：RGB SPI buffer、diag ring、`usb_comm` 线程栈 1024→768。
- 新加 keyboard-side oneof payload 前先用 `arm-zephyr-eabi-nm --size-sort build/<board>/zephyr/zmk.elf | findstr usb_.2._msg` 看当前 size；默认上 `FT_CALLBACK`，别走 `FT_STATIC` 大结构。
- dynamic 扩 proto 时记得在 `usb_comm.keyboard.options` 给 keyboard 补一条 `FT_IGNORE`，阻断跨板传染。
- dynamic 的 CCM 64K 目前完全未用，未来要更大缓冲可以考虑把"不经 DMA"的数据（eink framebuffer 等）挪进 CCM。

---

## 7. 常见改动"配方"

> 以下场景遵循：**先查是否已有 API → 再扩展 → 最后新写**。

### 7.1 我要加一个固件状态给上位机展示

1. 想清楚归属哪个已有 store（`rgb`/`knob`/`touchbar`/`function-slots`/…），**优先扩展现有消息**而不是加 Action。
2. 在 `usb_comm.proto` 扩 optional 字段（proto2 允许默认值缺省）。
3. 固件 handler 填 `has_xxx = true; xxx = ...`。
4. 上位机 `stores/*.ts` 的 `handleTransferIn` 处理 + `Vue` 里绑定。
5. 跑一次 `npm run build:proto`。

### 7.2 我要加一个新的 USB Action（无法塞进已有消息时再考虑）

参见 5.2 三件套，记得：Kconfig feature 开关（可选）→ `handler_version.c` 上报 → proto 加字段 → handler 注册 → host store 加 API → 路由/页面使用 → rebuild proto。

### 7.3 我要加一个自定义 RGB 效果

1. `app/hw75_rgb_effects.h` 加 `HW75_RGB_EFFECT_XXX` 枚举（接在最后，不要插中间）。
2. `usb_comm.proto::RgbState.Effect` 加同号枚举。
3. `config/app/rgb_effects.c`：
   - `zmk_rgb_underglow_custom_effects_mask()` 加位。
   - `zmk_rgb_underglow_custom_effect_render()` 的 switch 加分支。
   - 只在 `CONFIG_BOARD_HW75_KEYBOARD` 保护下。
4. `Rgb.vue` 的 effect label 表加标签。
5. rebuild proto + firmware。

### 7.4 我要加一个 keymap behavior（按键触发的新能力）

1. 绑定文件 `config/dts/bindings/behaviors/zmk,behavior-xxx.yaml`。
2. dtsi 片段 `config/dts/behaviors/xxx.dtsi`（用 `/omit-if-no-ref/` 避免未使用报错）。
3. 实现 `config/app/behaviors/behavior_xxx.c`，用 `BEHAVIOR_DT_INST_DEFINE` 注册 `on_binding_pressed/released`。
4. `config/app/CMakeLists.txt` 加 `zephyr_library_sources_ifdef`。
5. keymap 文件 `#include` dtsi 并使用。

### 7.5 我要加一个 TouchBar 段/改 TouchBar 行为

- 段数上限 `HW75_TOUCHBAR_MAX_SEGMENT_COUNT = HW75_TOUCHBAR_CHANNEL_COUNT = 6`。
- 手势时序常量在 `config/app/touchbar.c` 顶部 `#define`（TOUCHBAR_*_MS/TOUCHBAR_*_DISTANCE），**不要**散布到别处。
- 段 mask 的 proto 字段：用新的 `repeated uint32 segment_touch_masks / segment_entry_masks`（≤6）。老的 left/right 字段**仅保留兼容**，不要给它们加新语义。

### 7.6 我要加一个 helper 动作（本机执行命令/打开 URL）

1. `tools/hw75-helper/src/server.mjs::ACTIONS` 加一行（code 保持全局唯一、不复用老号）。
2. 实现同文件里的 `runAction(action, payload)` 分发分支（如果模式不同于已有）。
3. 上位机 `utils/helper-bridge.ts::HelperActionCatalogItem` 保持结构一致；页面渲染由 `routes/Keyboard.vue`（function slot 编辑器）自动适配。

### 7.7 我要加一个诊断事件

见 5.1。记住：`data0`/`data1` 是两个 uint32 数据位，跨字段打包用 `hw75_diag_pack_u8x4 / u16x2 / s16x2`（见 `diag_log.h`），上位机解码要对称拆。

---

## 8. 坑与反模式（**已经踩过、别重演**）

1. **HID 设备 slot 冲突**：`HID_0` 是 ZMK 键盘主设备；TouchBar 鼠标一定要 `HID_2`（见 `hw75_keyboard_defconfig` 的 `CONFIG_USB_HID_DEVICE_COUNT=3`、`CONFIG_HW75_HID_MOUSE_DEVICE_NAME`）。
2. **上位机不要并发多个请求**：必须过 `stores/usb.ts::send`；如果写了新 store，**禁止**直接 `comm.send(...)`。
3. **nanopb callback 写法**：
   - 先把数据拷贝到稳定数组（避免两次编码读不同源）。
   - 空数组就**不要**挂 callback（空 callback 会干扰后一个 callback）。
   - 不要混用 `FT_STATIC` + callback。
4. **大结构不要塞线程栈**：`MessageH2D/D2H` 已经是 static；新加 handler 内部也要留意 `usb_comm_LogEvent[N]` 这种，优先进 `ctx`（handler 静态）。
5. **诊断/临时代码必须打标记**：在 `handler_version.c` 的 `VER_APP` 后缀追加 `+xxx1` 之类（历史做法），并在 handoff 文档里登记；调查结束必须回退。
6. **不要动没改的代码去"优化"**：遵守用户规则"只在必要时重构"；先复用、再扩展、最后新写。
7. **改 proto 忘了 rebuild**：上位机和固件都读同一份 `.proto`，但生成产物独立（`comm.proto.{js,d.ts}` vs `usb_comm.pb.{c,h}`）。改完协议两边都要 rebuild。
8. **led_strip_remap label**：`indicator.c` 靠 `STRIP_INDICATOR_LABEL="STATUS"` 从 `led_strip_remap` 解出 status 段；改了 DTS 里 `status { label = "STATUS"; }` 就会断。
9. **keymap 里 6 个 touch 键留 `&none`**：TouchBar 已经被 74HC165 驱动独占原始态，keymap 里对应位置继续用 `&none` 保持矩阵完整，**不要**映射成真实按键。
10. **SRAM 临界**：任何"为了简化加个 buffer"的想法，先看 handoff 里 `+stackcb1 / +stackcb2` 那段，有多次教训。
11. **UART SLIP 发送超时机制**：`uart_slip.c::uart_slip_send` 现在用 `uart_fifo_fill()`（非阻塞，TXE 不 ready 立即返回 0）+ `k_yield()` 轮询代替原本的 `uart_poll_out()` 死等，整帧总超时 20ms（`UART_SLIP_SEND_BUDGET_MS`）。动因：dynamic 重置进 UF2 bootloader 时共用 TX 线被对端钉电平，原先 `uart_poll_out` 的 `while (!TXE)` busy-loop 会永远卡住（dynamic 后来恢复也救不回来，硬件状态机已死锁），进而拖死 keyboard 的系统 workqueue 或 ZMK 事件线程，整机 HID/keymap/LED 挂掉。改动后 keyboard 侧最坏情况 20ms 内 `uart_slip_send` 返回 `-ETIMEDOUT`，正常发送零影响（TXE 立即 ready 直接走过）。
12. **nanopb oneof 的 SRAM 陷阱 + 两个解耦工具**：`MessageH2D.payload` / `MessageD2H.payload` 是 `oneof`，nanopb 生成为 C `union`，**每个 static 实例 sizeof 取最大成员**。keyboard 20KB SRAM 极紧，如果 dynamic 在共享 `.proto` 里加一个大 payload（哪怕 keyboard 根本不处理它），keyboard 的 union 也会被撑大。共用 `.proto` 的两条破局工具（都走 per-board `.options`，见 §5.2）：
    - **`type:FT_IGNORE`**：某个 oneof 成员从本板生成的 C struct 里彻底消失——encode/decode 不生成、union 不占位、handler 不存在。用来让 keyboard 不为 dynamic-only 的 eink/knob/motor 付 SRAM；反过来也一样。
    - **`type:FT_CALLBACK`**：某个字段（`repeated` / 大嵌套 submessage）从"实体内嵌"退化成 8B `pb_callback_t`。真实数据住在 handler 的 static view，encode/decode 时流式读写。举例：`TouchbarConfig` 的 pan/app/desktop + 3 个 indicator + 2 个 masks 数组全 callback 后，它在 union 里从 ~340 B 降到 ~124 B（见 `handler_touchbar.c`）；老的 `EinkModeConfig.modes` / `LogEvents.events` 也是这套模式。
    - **守则**：加新 oneof 成员 / 新 `repeated` 前先 `arm-zephyr-eabi-nm --size-sort build/<board>/zephyr/zmk.elf | findstr usb_.2._msg` 看当前 size；默认走 callback，**只有**真需要简单同步访问才用 `FT_STATIC` + `max_count`。`deps/zmkx.app` 和 `tools/hw75-helper` 都用 protobufjs 读同一份 `.proto`，对 `.options` 里的 FT_IGNORE/FT_CALLBACK 完全透明——上位机不需要任何改动。
13. **keyboard HW75_UART_COMM：曾临时关闭，现已恢复 y**：2026-04 proto 扩展后 keyboard SRAM 落在 99.22%，dynamic 进 UF2 BL 触发 keyboard→dynamic uart_slip_send 峰值时栈越界，RGB 光效冻 + 按键失灵。当时作为快速修复把 `CONFIG_HW75_UART_COMM=n` 写入 defconfig。后通过 §5.2 的 per-board options + FT_CALLBACK 把 `usb_h2d_msg/d2h_msg` 从 368/356 B 砍到 156/144 B，SRAM 回落到 98.55%（margin ~296 B），UART link 稳定恢复。现在这条是 keyboard↔dynamic 所有联动功能的通道（FN 层状态、未来扩展），**不要**再关。

---

## 9. 工作流程约定

1. **动手前先找引用**：`rg '<symbol>'` / `rg '<function_name>'`，或 `Grep` 工具。新实现前先确认没有现成实现。
2. **改完检查残留**：删掉的函数/枚举/proto 字段在另一侧（固件↔上位机↔helper）如果还有引用，会编译过但运行错。
3. **本地验证最低线**：
   - 固件：`west build` 成功 + FLASH/SRAM 水位没有突然跳。
   - 上位机：`npm run build` 成功 + 本地 `npm run dev` 打开无红字。
4. **不要乱删 `MIGRATION_*.md` 条目**，它们是决策追溯。新重大结论可以在那里 append。
5. **本文件（AGENTS.md）过时的块要更新**，不要让它和源码漂移。更新时只改相关段落，不做大重构。

---

## 10. 快速决策清单

| 你想做                                   | 先去看                                                             |
| ---------------------------------------- | ------------------------------------------------------------------ |
| 加/改一个按键行为                        | `config/dts/behaviors/`、`config/app/behaviors/`、keymap 文件       |
| 加/改一个上位机页面                      | `deps/zmkx.app/src/routes/`、`src/stores/`、`src/pages/Main.vue` 侧栏 |
| 改 HID 报告                              | `config/app/hid_mouse.c`、`hw75_keyboard_defconfig`                  |
| 改 RGB 效果                              | `config/app/rgb_effects.c`（自定义）、`deps/zmk/app/src/rgb_underglow.c`（stock） |
| 改 TouchBar 时序/行为                    | `config/app/touchbar.c` 顶部 `#define`                              |
| 改 TouchBar 配置协议                     | `usb_comm.proto::Touchbar*` + `app/touchbar.h` + `handler_touchbar.c` + `Touchbar.vue` |
| 加一个 diag 事件                         | `app/diag_log.h` + `usb_comm.proto::LogEventId` + `debug_decode.ts` |
| 加一个 helper 动作                       | `tools/hw75-helper/src/server.mjs` + `utils/helper-bridge.ts`       |
| 改板级 DTS                               | `config/boards/arm/hw75_*/*.dts`、对应 `*_X_X_X.overlay`            |
| 改 Kconfig 选项                          | 板 `Kconfig.*` → 模块 `Kconfig` → `*_defconfig` 三路对齐            |
| 加新板子 revision                        | 新 `.overlay` + `.conf` + `board.cmake`/`revision.cmake`            |
| 改协议结构                               | `config/proto/usb_comm.proto` → rebuild 两端                         |

---

*本文档只描述"仓库现在是什么样、要改它应该去哪"。具体为什么当前是这样、历史上试过什么、踩过什么坑，去 `MIGRATION_HANDOFF*.md` 里查。*
