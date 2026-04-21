# 交接文档：HW-75 Dynamic 墨水屏多模式 / 旋钮零点 / Helper Core 升级

> 本文档是一次长对话末尾的 handoff。**下一个 agent 必读**，读完接着干活，别把前面的坑再踩一遍。
>
> 写作时间：2026-04-18
>
> 本文档的上位参考：[AGENTS.md](AGENTS.md)（项目级工作指南，本仓库唯一必读）。本文档是 AGENTS.md 的具体任务 context 补充。

---

## 0. TL;DR —— 用户当前在等什么

实机是 **hw75_keyboard@1.2 + hw75_dynamic@B**。需求：

1. Dynamic 墨水屏从"单张图上传"扩展成**多模式**（静态图 / 幻灯片 / 时间+天气 / 可扩展）
2. Dynamic 两颗按键**长按切换**模式
3. 电机旋钮**零点校准**
4. helper 升级成"上位机 core"（node-hid 直连键盘 + WebSocket 代理网页）
5. 最终数据流：`keyboard ⇄ helper-core ⇄ webapp`（不再 `keyboard ⇄ webapp ⇄ helper`）

整体大功能链路已经打通，但 **最后一轮用户反馈仍有一个关键问题未验证完**：

> "dynamic 进入 bl 之后，键盘还是会卡死（RGB 光效冻住、按键失灵），很像以前内存 crash"

我对此做了定位（keyboard SRAM 99.22% 过紧 → 运行时栈不够 → hardfault）并交付了修复版固件，**但用户还没刷这一版就结束了对话**。所以下一个 agent 的首要任务是：

> **等用户刷了最新 `build/keyboard12_fix/zephyr/zmk.uf2`（SRAM 降到 98.01%）+ `build/dynamicB/zephyr/zmk.uf2` 之后再看现象**：
> - 如果 keyboard 不再 crash → 根因确实是 SRAM，对话就结束了
> - 如果还 crash → 需要开 `CONFIG_HW_STACK_PROTECTION=y` + fault dump 抓 PC 定位

不要在用户反馈前贸然做新的大改动。

---

## 1. 项目与硬件速查（熟了跳过）

| 项 | 值 |
|---|---|
| 仓库根 | `E:\code\zmk-config_helloword_hw-75` |
| owner 实机 | `hw75_keyboard@1.2` + `hw75_dynamic@B` |
| Keyboard MCU | STM32F103XB (FLASH 104K / **SRAM 20K 极紧**) |
| Dynamic MCU | STM32F405XG (FLASH 320K 映射，另有 640K 未映射 / SRAM 128K + CCM 64K 未用) |
| 墨水屏 | 128×296 @ 1bpp, SSD16xx，约 4.7KB/帧 |
| USB VID/PID | `0x1D50 / 0x615E`（两板相同，用 product string 区分） |
| Product strings | `"HW-75 Keyboard"` / `"HW-75 Dynamic"` |
| UART 连接 | keyboard USART1 TX-only (PA9, 115200) → dynamic RX，单向 |
| Dynamic RTC | **没有**（DTS 无 rtc 节点、无 LSE 晶振、无 Vbat）—— 软时钟靠 helper 同步 |
| 日常编译 | `py -3.12 -m west build -p always -s deps/zmk/app -d build\<dir> -b "<board>@<rev>" -- "-DZMK_CONFIG=E:/code/zmk-config_helloword_hw-75/config" "-DKEYMAP_FILE=..."` |
| 上位机编译 | `cd deps/zmkx.app && node ./build-proto.mjs && npx vite build`（**别用 `npm run`，在这台机器上会卡**）|
| helper 依赖 | `node-hid`, `ws`, `protobufjs` |
| 启动开发环境 | `powershell -ExecutionPolicy Bypass -File .\start-hw75-dev.ps1` （已升级为"自动杀旧进程再起"）|

---

## 2. 整体方案（已落地）

### 2.1 架构变迁（见 [AGENTS.md §1.1](AGENTS.md)）

- **目标**：`keyboard ⇄ helper-core (node) ⇄ webapp (Vue)`
- **命名**：`tools/hw75-helper` 未来叫"上位机 core"，`deps/zmkx.app` 未来叫"上位机 UI"
- **迁移边界（当前状态）**：
  - 新功能（eink 多模式、时钟天气、knob 零点、未来 CPU 使用率等）**必须走 helper-core 新链路**（`stores/helperCore.ts::sendViaCore`）
  - 旧功能（RGB、knob_prefs、touchbar、function_slots、debug_log）暂保留 WebHID 直连，下次专门任务再迁移
  - 两套链路短期共存

### 2.2 数据流示意

```
  Keyboard (hw75_keyboard@1.2)          Dynamic (hw75_dynamic@B)
        │                                        │
        │ (USB HID keyboard reports)             │ (USB HID usb_comm: VID/PID + usage 0xff14)
        │                                        │
        ▼                                        ▼
    Windows OS                              node-hid (process: helper-core)
                                                 │
                                                 ▼
                                          WebSocket :8755/ws
                                                 │
                                                 ▼
                                            Vue webapp
                                      (浏览器 http://localhost:8080)
```

- helper 按 product name 过滤，**只连 dynamic**，不连 keyboard
- helper 承担：HID 直连键盘、定时任务（clock 每分钟 / weather 每 10 分钟）、WebSocket 代理给浏览器
- 协议仍然是单一源：`config/proto/usb_comm.proto`

---

## 3. 已完成的所有改动（按文件组织）

### 3.1 协议 & 固件共享

- **`config/proto/usb_comm.proto`**
  - 新 Action 22–29: `EINK_GET_CONFIG` / `SET_CONFIG` / `SET_ACTIVE` / `PUSH_FRAME` / `PUSH_CLOCK` / `PUSH_WEATHER` / `KNOB_GET_CALIBRATION` / `KNOB_SET_CALIBRATION`
  - 新消息: `EinkModeConfig` / `EinkModeEntry` / `EinkActiveRequest` / `EinkFrame` / `EinkClock` / `EinkWeather` / `KnobCalibration`
  - 新枚举: `EinkModeType` / `EinkWeatherIcon`
  - 新 `Features`: `eink_modes` / `knob_calibration`
  - 新 `LogModule`: `EINK=9` / `KNOB=10` / `HELPER_CORE=11`
  - 新 `LogEventId`: 120–122（eink 相关）/ 130（knob zero）/ 140–141（helper sync）
  - **关键**：`EinkModeConfig.modes` **必须保持 callback 式**（不带 `max_count`），否则会把 oneof union 撑到 >450B，**直接让 keyboard SRAM 爆** —— 见下文 §5.2 坑表

- **`config/app/include/app/diag_log.h`**：与 proto 的 `LogModule` / `LogEventId` 同步追加

- **`config/app/usb_comm/usb_comm_proto.c`**
  - `h2d_callback` 追加两条路由：`eink_frame.bits` → `read_bytes_field`；`eink_mode_config` → `usb_comm_eink_mode_prepare_decode`
  - 条件编译 `#if defined(CONFIG_HW75_EINK_MODES)`，保证 keyboard 不引入 dynamic-only 符号

- **`config/app/usb_comm/handler/handler_eink_mode.c`**（新）
  - 6 个 handler：`EINK_GET_CONFIG` / `SET_CONFIG` / `SET_ACTIVE` / `PUSH_FRAME` / `PUSH_CLOCK` / `PUSH_WEATHER`
  - `encode_modes` / `decode_mode_entry` 作为 callback 编码/解码 repeated 字段
  - **使用 static `g_encode_view` / `g_decode_view`**（各 ~448B），保证 callback 生命周期内有稳定内存；dynamic-only，不影响 keyboard
  - `usb_comm_eink_mode_prepare_decode(cfg)` 供 `usb_comm_proto.c` 在解码前调用

- **`config/app/usb_comm/handler/handler_knob.c`**
  - 新增 `KNOB_GET_CALIBRATION` / `KNOB_SET_CALIBRATION`
  - **重点**：返回的 `zero_offset` 是 **knob position offset**（用户白线零点），不是 motor FOC 零点。FOC direction 仅作为 read-only 展示，UI 不能修改

- **`config/app/usb_comm/handler/handler_version.c`**：按 `CONFIG_HW75_EINK_MODES` / `CONFIG_HW75_KNOB_CALIBRATION_PERSIST` 上报新 feature 位

### 3.2 Dynamic 专属

- **`config/boards/arm/hw75_dynamic/hw75_dynamic.dts`**：`eink_frames_partition` @0x60000, 256K（2×128K sector，NVS 用；单 128K 不够，因为 NVS 需要至少 2 sector 做 GC）

- **`config/boards/arm/hw75_dynamic/app/eink_mode.{c,h}`**（新）
  - 模式引擎：`OFF` / `STATIC` / `SLIDESHOW` / `CLOCK_WEATHER`
  - NVS 存配置 + frame bitmaps（`EINK_NVS_KEY_CONFIG=1`, `EINK_NVS_KEY_FRAME_BASE=0x1000`）
  - `k_work_delayable` 按 `refresh_interval_s` 轮播
  - **软时钟**：`eink_mode_push_clock` 时记下 `k_uptime_get()` 作为 anchor；`clock_tick_handler` 按分钟边界 `reschedule` 自己推进；helper 断开也能继续走
  - **上电不强制刷屏**：`render_clock_weather()` / `render_static()` / `render_slideshow()` 在无数据时都 return 0（墨水屏硬件残留保持上次图像）
  - `apply_default_view()` 默认给 3 个 mode: Clock / Image / Off，开箱按键切换就有内容

- **`config/boards/arm/hw75_dynamic/app/eink_render.{c,h}`**（新）
  - 7-seg 风格算法渲染（无字库依赖，省 flash）
  - 8 种天气 icon 纯算法画（sun / cloud / rain / snow / storm / fog / moon / unknown）
  - 布局：HH:MM 大（y=18）/ MM/DD 小 / divider / 天气 icon / 温度

- **`config/boards/arm/hw75_dynamic/app/knob_app.{c,h}`**
  - **重点**：`knob_app_set_calibration(position_offset, direction)` **完全不调 `motor_calibrate_set`**，只调 `knob_set_position_offset`。动机见 §4.2
  - `knob_app_recalibrate_auto()` 走 auto cal 流程（清除 user offset）
  - settings 存 `struct knob_calibration_record { float position_offset; uint8_t valid; ... }`
  - 上电 auto cal 完成后，如 settings 有有效 offset，调 `knob_set_position_offset` 恢复

- **`config/drivers/sensor/knob/knob.{c}` + `include/knob/drivers/knob.h`**
  - 新 API: `knob_set_position_offset(dev, offset)` / `knob_get_position_offset(dev)`
  - `position_offset` 是全局的 float，单位 radians

- **`config/drivers/sensor/knob/profile/spring.c`**
  - 之前 `data->center = deg_to_rad(180)` 硬编码
  - 现在 `knob_spring_tick` 每帧读 `knob_get_position_offset(cfg->knob)` 作为 `mc->target`（回中目标角度）
  - 触发区间：±dead_zone around `centre`

- **`config/hw75_dynamic.keymap`**
  - 引入 ZMK 原生 `&ek_ht`（hold-tap, tapping-term-ms = 350, flavor = "tap-preferred"）
  - 短按 → `&lvkp` (LVGL 导航，原行为)
  - 长按 → `&ekcyc ±1` (切墨水屏模式)
  - Volume 层绑 `&ek_ht -1 LV_KEY_PREV` / `&ek_ht 1 LV_KEY_NEXT`，其他层继续 `&trans`（fall-through 到 volume）

- **`config/app/behaviors/behavior_eink_mode_cycle.c`**（新）+ **`config/dts/behaviors/eink_mode_cycle.dtsi`**（新）+ **`config/dts/bindings/behaviors/zmk,behavior-eink-mode-cycle.yaml`**（新）
  - `&ekcyc <delta>`：press 触发 `eink_mode_cycle(delta)`
  - `delta=0` 时当 `+1` 处理（防御性）

- **`config/boards/arm/hw75_dynamic/app/Kconfig.eink`**：`HW75_EINK_MODES` 默认 y（dynamic 下）
- **`config/boards/arm/hw75_dynamic/app/Kconfig.knob`**：`HW75_KNOB_CALIBRATION_PERSIST` 默认 y

### 3.3 Keyboard 专属

- **`config/drivers/console/uart_slip.c`**（共享驱动但只 keyboard 用 send）
  - `uart_slip_send` 重写：用 `uart_fifo_fill()` 非阻塞 + `k_yield()` + 整帧 **20ms deadline**（`UART_SLIP_SEND_BUDGET_MS`）
  - 取代原来的 `uart_poll_out()` 死等 TXE（TXE 在对端拉异常电平时**永远**不回来，dynamic 后续恢复也救不回硬件死锁状态）

- **`config/boards/arm/hw75_keyboard/app/uart_comm/uart_comm.c`**
  - 加连续失败短路：连续 3 次 `uart_slip_send` 超时 → 暂停 **5 分钟**不再发（`UART_COMM_MAX_CONSECUTIVE_FAILURES=3` / `UART_COMM_BACKOFF_MS=300000`）
  - 下次成功时自动清 streak

- **`config/boards/arm/hw75_keyboard/app/uart_comm/report/report_ping.c`**：`PING_REPORT_INTERVAL_MS` 5000 → **60000**（ping 每分钟一次，降低 sys_workq 被 blocking 20ms 的频率）

- **`config/boards/arm/hw75_keyboard/app/uart_comm/Kconfig`**：`HW75_UART_COMM` 加 prompt，可被 defconfig 覆盖

- **`config/boards/arm/hw75_keyboard/hw75_keyboard_defconfig`**：**`CONFIG_HW75_UART_COMM=n`**（当前状态）
  - 关键动机：proto 扩展后 keyboard SRAM 99.22%（20320/20480B），运行时栈不够用，出现 RGB 冻 + 按键失灵（hardfault/stack overflow）
  - 关掉后 SRAM 98.01%（20072/20480B），margin ~400B，和历史稳定版同水位
  - 代价仅：dynamic 状态 OLED 上少一个 "FN 层" 小指示符。keyboard 核心功能**不受影响**。

### 3.4 Helper (tools/hw75-helper)

- **`package.json`**：version 0.3.0，新 deps：`node-hid`, `ws`, `protobufjs`
- **`src/server.mjs`**：整合所有模块，新增 `GET /api/helper-core/status`，启动时挂 WebSocket
- **`src/protoLoader.mjs`**（新）：**运行时**读 `config/proto/usb_comm.proto`，用正则剥除 `import "nanopb.proto"` / `[(nanopb)...]` / `option (nanopb_msgopt)...` 后交给 `protobufjs.parse`。**不依赖**上位机生成的 `comm.proto.js`（那份 ESM 在 node 里会触发 `protobufjs/minimal` 解析问题）
- **`src/keyboard.mjs`**（新）
  - `node-hid` 连接，**按 product name 含 "dynamic" 过滤**
  - 启动枚举时打印所有 HW-75 HID 接口（product / usagePage / interface / path），方便排查
  - 分包：63B HID report, report_id=1, 第 1 byte = 长度，剩下是 payload
  - 单 slot 串行化（与 keyboard 固件的 usb_comm 单 RX slot 对称）
  - 超时 2000ms，失败自动重连
- **`src/bus.mjs`**（新）：WebSocket 桥，二进制帧 `[type(1) | req_id(4) | body]` 走 H2D/D2H，文本 JSON 走 status/config/events
- **`src/coreConfig.mjs`**（新）：持久化 `%APPDATA%\hw75-helper\core-config.json`（weather/clock 配置）
- **`src/weather.mjs`**（新）：open-meteo API，10min 拉一次 → 编码 `EINK_PUSH_WEATHER` 推给键盘
- **`src/clock.mjs`**（新）：每分钟边界 push

### 3.5 上位机 (deps/zmkx.app)

- **新 stores**：
  - `src/stores/helperCore.ts` - WebSocket 客户端，`sendViaCore(h2d)` 返回 `Promise<MessageD2H>`，`onKeyboardEvent(cb)` 订阅异步事件
  - `src/stores/knob-calibration.ts` - 走 helperCore 发 `KNOB_GET/SET_CALIBRATION`

- **改写 `src/stores/eink.ts`**：保留 legacy `einkImage` ref（usb.ts 兼容），新 API（`getConfig` / `setConfig` / `setActive` / `pushFrame`）全部走 helperCore

- **`src/routes/Eink.vue` 完全重写**：左侧 mode 列表（新建/删除/激活/保存）+ 右侧按 type 切换编辑器（Static / Slideshow / Clock+Weather / Off）

- **新组件 `src/components/eink/StaticEditor.vue`**：复用 `utils/graphic.ts` 的 `binarize/toBits/scaleInside`

- **`src/routes/MotorDemo.vue`**：
  - 新增"旋钮零点（白线基准）"滑动条，范围 ±π，步进 0.001
  - 配套 "读取当前" / "恢复自动校准" 两个按钮
  - i18n 文案**强调"仅影响旋钮行为，不动电机 FOC"**（避免误解为 motor_calibrate_set）
  - 只在 `version?.features?.knobCalibration` 为真时显示

- **`src/stores/debug_decode.ts`**：同步新 `LogModule` / `LogEventId` 的渲染映射

- **`src/pages/Main.vue`**：eink 菜单条件 `version?.features?.eink || version?.features?.einkModes`（向后兼容旧固件）

### 3.6 开发脚本

- **`start-hw75-dev.ps1`** 升级
  - 默认行为 = **自动杀旧进程再起**（你反复改 uf2 的测试流程必备）
  - 双路径清理：命令行匹配 `node/npm/cmd` + 按端口（8755 / 8080）占用查 PID 双路径杀
  - 用 `netstat -ano | Select-String` 代替 `Get-NetTCPConnection`（后者在用户机上会卡）
  - 400ms 等 Windows 释放 socket 再绑定
  - 加 `-NoRestart` 参数保留原"已运行就不动"行为
  - 加 `-OpenBrowser` 参数自动开浏览器

### 3.7 AGENTS.md 更新（重要！）

新增或改动的章节：

- **§1** 表格后新增一行："本仓库 owner 的实机：`hw75_keyboard@1.2` + `hw75_dynamic@B`"
- **§1.1 产品架构演进目标**（新章节）：helper-core 角色变迁、新旧链路分界、命名约定
- **§3.1 固件本地编译**：给出 keyboard12 + dynamicB 两条完整命令
- **§6 内存约束表**：最新实测 SRAM/FLASH 水位
- **§8 坑与反模式**新增三条：
  - **#11** UART SLIP 超时机制
  - **#12 nanopb oneof SRAM 陷阱**（最重要，本次的主要坑）
  - **#13** keyboard `HW75_UART_COMM` 当前 =n 的原因和重开条件

---

## 4. 关键设计决策

### 4.1 为什么 EinkModeConfig.modes 必须是 callback

`usb_comm_MessageH2D.payload` 和 `MessageD2H.payload` 是 `oneof`，nanopb 生成为 C `union`，**size 取最大成员**。

第一版我用了 `repeated EinkModeEntry modes = 1 [(nanopb).max_count = 8]`，nanopb 生成了 `EinkModeEntry[8]` static 数组。每个 `EinkModeEntry` 带 `label[25]` + 几个 u32 + has_flags ≈ 56B。8 个 = 448B。加上 `active_index` 等，`EinkModeConfig` ≈ 468B。

**oneof 选最大，MessageH2D 和 MessageD2H 两个 static 实例各吃 ~470B，共 ~940B SRAM**。keyboard 链接 SRAM 99.41% 过得去但运行时栈空间被榨干 → hardfault。

改 callback 后 `EinkModeConfig` 只剩 `pb_callback_t modes` (8B) + 少量 flag，整体 ~24B。union max 回落到 `TouchbarConfig` (~360B)。

**规则**：任何 repeated 字段默认走 callback；只有真需要简单同步访问才加 `max_count`。上位机侧（protobufjs）对 callback 透明，TS 类型完全不变。

### 4.2 为什么 knob 零点不能调 motor FOC

**第一版我理解错了**：以为用户要调的零点是 `motor_calibrate_set(motor, zero_offset, direction)`。结果：spring 模式下白线飞到屏幕最上面看不见。

**正确理解**：
- `motor_data->zero_offset` 是 **FOC 电相位对齐**，由 `motor_calibrate_auto` 开机时根据编码器和电流反馈自动算出。调错了电机转动方向和平稳性全毁。**不能给用户动**。
- 用户要调的"白线零点" = spring 模式的回中目标角度。旧代码 `data->center = deg_to_rad(180)` 硬编码，根本没考虑物理装配时白线指向哪个角度。

**最终方案**：
- 加 `knob_set_position_offset(dev, offset)` / `knob_get_position_offset` API
- `spring.c::knob_spring_tick` 每帧读 offset 作为 `mc->target`
- 用户通过 UI 调整 offset → spring 模式立即响应（电机转到新的中心位置）
- `motor_calibrate_set` 只在 `motor_calibrate_auto` 内部调用，user code 不碰

### 4.3 为什么 helper 必须按 product 过滤

Keyboard 和 Dynamic 用相同 VID/PID（ZMK 默认 0x1D50/0x615E）。两者 USB HID 都暴露 `usage_page=0xff14` 的 `usb_comm` 接口。node-hid 默认按 VID/PID 过滤会同时匹配两个。

helper 只应该连 dynamic（新功能都在 dynamic），连错 keyboard 会：
- keyboard 的 usb_comm handler 不认识 Action 22-29 → 按默认流程返回 Nop（**应该能 match，但前提是 keyboard 没 crash**）
- helper 会误把 keyboard 的主 HID 键盘报告解析成 D2H → decode fail

**最终方案**：`matchesUsage()` 加 `info.product.toLowerCase().includes('dynamic')`。Windows 上 node-hid 会返回 USB iProduct string（`CONFIG_USB_DEVICE_PRODUCT`，分别是 "HW-75 Dynamic" / "HW-75 Keyboard"）。空 product 时 refuse 连接（安全第一）。

并在启动时打印枚举到的所有 HW-75 接口，方便排查。

### 4.4 为什么 uart_slip 要走非阻塞 + 超时

`uart_poll_out()` 底层：
```c
while (!LL_USART_IsActiveFlag_TXE(UART)) {}
UART->DR = byte;
```

dynamic 进 UF2 bootloader 时，它的 USART RX pin 可能被 bootloader 重配成 GPIO 并拉到异常电平。这会让 keyboard 的 USART TX 当前字节**移位失败**，TXE 标志不再翻回 1。**硬件状态机死锁**，dynamic 后来恢复也救不回来。

原代码：`uart_poll_out` 在 `while (!TXE)` 里转 → busy-loop 永远不退出 → 占住 cooperative 优先级的 sys_workq → keyboard HID/keymap/LED 全冻。

新方案：用 `uart_fifo_fill(uart, &b, 1)`（非阻塞，TXE 不 ready 直接返回 0）+ 整帧 20ms deadline + `k_yield()` 让其他线程喘气。最坏情况 20ms 返回 `-ETIMEDOUT`。

### 4.5 为什么 dynamic 要"不主动刷墨水屏"

第一版我在 `apply_default_view` 后立即 `trigger_refresh(RENDER_REASON_ACTIVE, K_SECONDS(2))`。2s 后 render_clock_weather 被调用，但此时 `g_clock.valid = g_weather.valid = false`，画出来就是 "00:00 0°C" 的默认值。墨水屏本来硬件残留着上次开机的图（可能是用户上次自己的时间天气），被强制覆盖。

**正确行为**：墨水屏硬件断电保持图像是个优势，**不要主动破坏**。`render_clock_weather / render_static / render_slideshow` 在无数据时都 return 0，让屏幕保持原状。helper 第一次 push 进来后才正常渲染。

### 4.6 为什么 dynamic 要有软时钟

Dynamic STM32F405XG 虽然芯片有 RTC 外设，但 DTS 没有 rtc 节点，硬件上也没接 LSE 晶振 + Vbat 电池。所以**真 RTC 做不到**（断电即丢）。

软时钟方案：
- `eink_mode_push_clock(hour, min, ...)` 时记下 `k_uptime_get()` 作为 anchor
- `k_work_delayable clock_tick_work` 按 60s 边界 reschedule，每次 fire 把 `g_clock.minute++`
- helper 每分钟再 push 一次，校正 k_uptime 累积的漂移

效果：helper 同步一次后，拔 helper 也能继续走时间；断电重启会丢（下次启动显示 eink 硬件残留的旧图）。

### 4.7 为什么 keyboard 最终关掉了 HW75_UART_COMM

即使 `EinkModeConfig.modes` 改了 callback + `uart_slip_send` 加了超时，keyboard SRAM 仍然落在 99.22%。用户反馈 keyboard 依然会 crash（"RGB 光效冻 + 按键失灵"，像以前内存 crash）。

决策：干脆关掉 `HW75_UART_COMM`（ping + FN-state 上报）一条链路，直接把 SRAM 降到 98.01%（margin ~400B，和历史稳定版同水位）。代价仅是 dynamic 状态 OLED 上少一个 FN 层指示符。

要再启用必须先腾出 ≥300B SRAM（候选：ws2812 SPI buffer 分块 DMA、diag ring 缩小、usb_comm 线程栈减到 768）。

---

## 5. 用户这次反馈的 6 个问题 & 修复状态

| # | 现象 | 根因 | 修复 | 需要用户验证 |
|---|---|---|---|---|
| 1 | Dynamic 进 BL → keyboard 卡死 | keyboard SRAM 99.4% 爆栈 hardfault（非 UART 问题） | proto callback + `HW75_UART_COMM=n` → SRAM 98.01% | **是（刷最新 uf2）** |
| 2 | 调零点后 spring 白线飞到最上 | 改错了目标——动到了 motor FOC 零点 | 新增 `knob_set_position_offset`，spring 用它作 center，FOC 不动 | 是 |
| 3 | helper 报 HID error / Action timeout | helper 连错了 HID 接口（VID/PID 相同 + 无 product 过滤） | `matchesUsage` 按 product 过滤 "dynamic"，启动打印枚举 | 是 |
| 4 | Dynamic 重启后墨水屏变 "00:00 0°C" | render_clock_weather 在无数据时强制画默认值 | 无数据时 return 0 不刷屏 | 是 |
| 5 | 按键切模式只有时间天气 | 默认 mode 只有 1 个 CLOCK_WEATHER，加其他 mode 被 helper-连-错断了 | `apply_default_view` 默认 3 个 mode + helper 过滤修好 | 是 |
| 6 | 键盘能自己记时间吗 | 硬件没 RTC | 软时钟：anchor uptime + 每分钟 tick 自己推进 | 是 |

---

## 6. 最新构建产物（用户还未全部刷）

| 固件 | 路径 | FLASH | SRAM |
|---|---|---|---|
| keyboard@1.2 | `build/keyboard12_fix/zephyr/zmk.uf2` | 72.38% | **98.01%**（20072/20480） |
| dynamic@B | `build/dynamicB/zephyr/zmk.uf2` | 56.80% | 58.85%（77136/131072） |

上位机无需重建产物（只改了 i18n 和 UI 逻辑，用户用 `npm run dev` 跑就是最新）。

---

## 7. 下一个 agent 接手时的注意事项

### 7.1 别踩的坑

1. **`npm run <script>` 在这台机器上会卡** —— 用 `node ./script.mjs` 直接跑
2. **`Get-NetTCPConnection` 会卡** —— 用 `netstat -ano | Select-String`
3. **PowerShell 脚本不能用 `$pid` 作变量/参数名** —— 它是自动变量
4. **node 的 `import(absolutePath)` 在 Windows 不行** —— 要 `pathToFileURL(path).href`
5. **protobufjs 不能解析 nanopb 扩展** —— 读 `.proto` 前先剥除 `[(nanopb)...]` 注解和 `option (nanopb_msgopt)...`
6. **任何新 `repeated` proto 字段默认走 callback** —— 加 `max_count` 前必查 keyboard 的 `usb_.2._msg` size：
   ```powershell
   & "$env:ZSDK\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe" --size-sort -r build\keyboard12_fix\zephyr\zmk.elf | findstr usb_
   ```
7. **keyboard SRAM 任何改动都要看 map**，margin 只有 400B

### 7.2 优先顺序

1. **先等用户刷最新 uf2 的反馈**，别贸然做新改动
2. 如果 keyboard 仍 crash，开 `CONFIG_HW_STACK_PROTECTION=y` + `CONFIG_EXCEPTION_STACK_TRACE=y`（或类似）抓 PC，看死在哪行
3. 如果 helper timeout 仍存在（但 keyboard 不 crash），用新加的枚举日志确认连的是 dynamic
4. 只有 1-3 都稳了，再讨论剩下的优化（比如把老功能迁移到 helper-core 链路、RTC 硬件支持、flash 分区扩展等）

### 7.3 产品愿景路线图（来自用户原话）

> "现在他叫 helper，后面他就应该叫上位机 core 了，现在的所谓上位机，以后只不过是我们真正完整版上位机的 ui 组件。"

- helper-core 持有业务状态；webapp 只做展示
- 以后比如键盘实时获取 CPU 使用率：helper 自己查、发给键盘、同时推给 webapp 展示
- 天气 / 时间、function-slot 的 helper action：都是 helper 主导，webapp 只配置
- **不要丢这个方向**

---

## 8. 给用户的刷写说明（贴到末尾方便用户操作）

```
1. 插 keyboard → 双击 reset 进 BL → 拖 build/keyboard12_fix/zephyr/zmk.uf2
2. 插 dynamic  → 进 BL → 拖 build/dynamicB/zephyr/zmk.uf2
3. powershell -ExecutionPolicy Bypass -File .\start-hw75-dev.ps1
4. 浏览器 http://127.0.0.1:8080/
5. 观察 helper 控制台打印：
   [keyboard] hw75 HID interfaces found: N
     - product="HW-75 Keyboard" ...
     - product="HW-75 Dynamic" ...
   [keyboard] connected: \\?\HID#VID_1D50&PID_615E&MI_01#...
6. 验证项：
   a) dynamic 进 BL 时 keyboard 是否卡？(修问题 1)
   b) spring 模式的白线零点可以滑条调吗？(修问题 2)
   c) helper 是否还 timeout？(修问题 3)
   d) dynamic 重启后墨水屏是保持还是被刷白？(修问题 4)
   e) 按键长按 350ms 是否能在 3 个 mode 间循环？(修问题 5)
   f) 拔掉 helper 后时间是否还在走？(修问题 6)
```

---

## 9. 2026-04-19 Follow-up

> 这一节续接上面 §0–§8，记录 2026-04-19 对话的增量状态，不重复已覆盖内容。

### 9.1 这轮解决了什么

**问题 1（keyboard 在 dynamic 进 BL 时卡死）根因重新确认并彻底修复**：

上一轮的快修是 `CONFIG_HW75_UART_COMM=n`（代价：dynamic 状态 OLED 没 FN 层指示，未来所有 keyboard↔dynamic 联动都没通道）。本轮做了**真正的架构修复**，UART_COMM 现已 =y：

- `config/proto/usb_comm.{keyboard,dynamic}.options`（新）+ `config/proto/CMakeLists.txt`：per-board nanopb options。keyboard 侧把所有 dynamic-only payload 标 `FT_IGNORE`（eink/knob/motor），把 TouchbarConfig 的 6 个嵌套子 config + 2 个 mask 数组标 `FT_CALLBACK`；FunctionSlotConfig/Caps/Events 的 3 个 repeated 字段也标 `FT_CALLBACK`。dynamic 侧反之（IGNORE touchbar / function_slot）。
- `config/app/usb_comm/handler/handler_touchbar.c` 重写：8 个 callback encode/decode + `usb_comm_touchbar_prepare_decode()` 接口给 `usb_comm_proto.c` 在 decode 前挂钩。
- `config/app/usb_comm/handler/handler_function_slot.c` 重写：3 个 callback + `usb_comm_function_slot_prepare_decode()`。
- `config/app/usb_comm/usb_comm_proto.c::h2d_callback`：改成无条件挂到 `h2d->cb_payload`，内部按 feature 分发 touchbar / function_slot / eink_mode / bytes_field 的 decode route。
- `config/boards/arm/hw75_keyboard/hw75_keyboard_defconfig`：删掉 `CONFIG_HW75_UART_COMM=n`（回默认 y）。

**结果（hw75_keyboard@1.2，`build/keyboard12_v5`）**：
- `usb_h2d_msg`：368 B → **156 B**
- `usb_d2h_msg`：356 B → **144 B**
- SRAM：99.22% (20320 B) → **98.55% (20184 B)**，margin ~296 B
- UART_COMM=y 稳定，dynamic 进 UF2 BL 时 keyboard 不再卡死 —— 用户实机确认 ✅

**副作用红利**：
- dynamic（`build/dynamicB_v2`）也享受 FT_IGNORE，`usb_h2d_msg` 368 B → **64 B**，`usb_d2h_msg` 356 B → **76 B**。未来 dynamic 扩 eink/knob 字段，keyboard 不再受连累。
- AGENTS.md §2/§4.3/§5.2/§6/§8 同步更新；§8 #13 从"临时关闭中"改为"已恢复 y，不要再关"。

### 9.2 helper node-hid reportId 修复

`tools/hw75-helper/src/keyboard.mjs::handleRx` 原本没剥 HID report id 前缀（Windows `node-hid` 对 numbered reports 会保留 `data[0] = reportId`，和 WebHID 自动剥离的行为不一致），导致所有 D2H 响应 decode 失败，表现为 `[clock] push failed: Action 26 response timeout` + `decode failed: missing required 'action'` 循环。修改后 `handleRx` 按 `data.length > HID_REPORT_SIZE` 判断是否有 reportId 并跳过，问题 3 彻底消除。

### 9.3 本轮 commit 范围 vs 工作目录暂存

**已 commit**（本轮作为 git 里第一个大 milestone）：

- 固件层：`config/proto/{CMakeLists.txt, usb_comm.keyboard.options, usb_comm.dynamic.options}`, `config/app/usb_comm/usb_comm_proto.c`, `config/app/usb_comm/handler/handler_{touchbar,function_slot}.c`, `config/boards/arm/hw75_keyboard/hw75_keyboard_defconfig`
- 文档：`AGENTS.md`, `HANDOFF_EINK_MULTIMODE.md`（含本节）

**仍在工作目录暂存**（owner 未来决定如何打包）：

- `tools/hw75-helper/`：整个目录从未 track 过。本轮虽然修复了 `keyboard.mjs` 的 reportId 问题，但 `server.mjs / bus.mjs / coreConfig.mjs / protoLoader.mjs / weather.mjs / clock.mjs` 都是前一轮引入且未 commit，`keyboard.mjs` 依赖它们。要 commit 就得整个目录一次 add，属于 owner 级决策，本轮不碰。
- `deps/`（vendored ZMK + zmkx.app）：同上，从未 track。
- 其余 untracked：`MIGRATION_*.md`, `start-hw75-dev.{cmd,ps1}`, `west.yml`, `config/app/{diag_log,function_slot,touchbar,rgb_effects}.c`, `config/boards/arm/hw75_dynamic/app/{eink_mode,eink_render}.c`, 大量 `config/dts/behaviors/*.dtsi` 等 —— 都是前几轮对话遗留，本轮不整合。

### 9.4 仍未验证（不确定改动后是否有回归 bug）

刷 `keyboard12_v5` + `dynamicB_v2` 后，下列功能**代码路径已换**但**还没上位机交互实测**：

- **TouchBar UI**：`TOUCHBAR_GET_CONFIG / SET_CONFIG` 现在走 callback，请验证 Touchbar.vue 页能读出配置、改参数保存后再读回值一致
- **Function Slot UI**：5 个 slot 配置的 GET/SET、HID preset 列表、trigger event 日志

以及上一轮本来就未验证（§5 的问题 2/4/5/6）：

- knob 零点（MotorDemo 页滑条）
- 墨水屏断电残留是否保持
- dynamic 长按两颗键循环 3 个 mode
- 拔 helper 后墨水屏软时钟是否继续

### 9.5 下一任务（已在本轮最后由 owner 指定）

**dynamic 墨水屏局部刷新**：现在 eink 每次更新都走全刷（屏幕黑→白→显示），体验差。目标是时间/日期/天气温度的局部变化走 SSD16xx 的 partial update，只有累积残影过多时才触发一次 full refresh。进入点：`config/boards/arm/hw75_dynamic/app/eink_mode.c` + `config/drivers/display/ssd16xx.c`。本轮不再 commit，新改动直接叠加到工作目录。

