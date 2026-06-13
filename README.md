ZMK for HW-75 [![.github/workflows/build.yml](https://github.com/xingrz/zmk-config_helloword_hw-75/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/xingrz/zmk-config_helloword_hw-75/actions/workflows/build.yml)
========

[![license][license-img]][license-url] [![issues][issues-img]][issues-url] [![commits][commits-img]][commits-url] [![releases][releases-img]][releases-url] [![downloads][downloads-img]][releases-url]

![HW-75](https://github.com/peng-zhihui/HelloWord-Keyboard/raw/main/5.Docs/2.Images/hw1.jpg)

Build note: GitHub Actions now compiles against the vendored `deps/zmk` source tree in this repository instead of fetching `xingrz/zmk` during CI.

[瀚文 75 (HW-75)](https://github.com/peng-zhihui/HelloWord-Keyboard) 是一款由稚晖君 ([@peng-zhihui](https://github.com/peng-zhihui)) 设计并开源的模块化机械键盘。

本仓库是针对 HW-75 的 [ZMK](https://github.com/zmkfirmware/zmk) 编译配置。

HW-75 由键盘主体 (Keyboard) 与扩展模块 (Dynamic) 组成。详细说明请见：

* [HW-75 Keyboard](config/boards/arm/hw75_keyboard/)
* [HW-75 Dynamic](config/boards/arm/hw75_dynamic/)

## 仓库结构与构建 / Repository layout & building

本仓库不只是编译配置，还内含**魔改 ZMK 固件源码**与**配套中枢**。四个子树：

* `config/` — Zephyr module：板级定义、自定义驱动、app 层 C 代码、protobuf 协议（`proto/usb_comm.proto` 是固件与中枢共用的唯一协议源）
* `deps/zmk/` — vendored 的魔改 ZMK 分支（CI 与本地编译都用这份，不再外拉上游）
* `deps/zmkx.app/` — vendored 的 Vue3 中枢（浏览器配置界面）
* `tools/hw75-core/` — Windows 本地 helper 服务

两块硬件目标共享同一套代码：`hw75_keyboard`（STM32F103XB，主键盘 + TouchBar）与 `hw75_dynamic`（STM32F405XG，旋钮 + 墨水屏扩展模块）。

固件本地编译（PATH 需有 protoc / dtc / ninja / cmake / Python 3.12 + Zephyr SDK）：

```powershell
py -3.12 -m west build -p always -s deps/zmk/app -d build\keyboard12 `
    -b hw75_keyboard@1.2 `
    -- "-DZMK_CONFIG=$PWD/config" "-DKEYMAP_FILE=$PWD/config/hw75_keyboard.keymap"
```

中枢：`cd deps/zmkx.app && npm ci && npm run dev`（vite dev on :8080）。

> **贡献者 / AI agent 请先读 [`AGENTS.md`](AGENTS.md)** —— 架构与构建的权威指南；[`CLAUDE.md`](CLAUDE.md) 是它的精简入口。

## 相关链接

* [peng-zhihui/HelloWord-Keyboard](https://github.com/peng-zhihui/HelloWord-Keyboard)
* [ZMK Firmware](https://zmk.dev/)

## 协议

[MIT License](LICENSE)

[license-img]: https://img.shields.io/github/license/xingrz/zmk-config_helloword_hw-75?style=flat-square
[license-url]: LICENSE
[issues-img]: https://img.shields.io/github/issues/xingrz/zmk-config_helloword_hw-75?style=flat-square
[issues-url]: https://github.com/xingrz/zmk-config_helloword_hw-75/issues
[commits-img]: https://img.shields.io/github/last-commit/xingrz/zmk-config_helloword_hw-75?style=flat-square
[commits-url]: https://github.com/xingrz/zmk-config_helloword_hw-75/commits/master
[releases-img]: https://img.shields.io/github/v/release/xingrz/zmk-config_helloword_hw-75?include_prereleases&style=flat-square
[releases-url]: https://github.com/xingrz/zmk-config_helloword_hw-75/releases/latest
[downloads-img]: https://img.shields.io/github/downloads/xingrz/zmk-config_helloword_hw-75/total?style=flat-square
