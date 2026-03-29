# Copyright (c) 2022-2023 XiNGRZ
# SPDX-License-Identifier: MIT

# report - ping

zephyr_library_sources(report_ping.c)

# report - fn-state (optional: see HW75_UART_FN_STATE_REPORT)

zephyr_library_sources_ifdef(CONFIG_HW75_UART_FN_STATE_REPORT report_fn_state.c)
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
