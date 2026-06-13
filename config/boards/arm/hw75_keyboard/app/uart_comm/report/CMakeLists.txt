# Copyright (c) 2022-2023 XiNGRZ
# SPDX-License-Identifier: MIT

# report - ping

zephyr_library_sources(report_ping.c)

# report - fn-state

zephyr_library_sources(report_fn_state.c)

# report - layer index / caps (knob haptic feedback over UART)

zephyr_library_sources(report_layer_index.c)
zephyr_library_sources(report_caps.c)

zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
