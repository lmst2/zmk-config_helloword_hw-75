# Copyright (c) 2022-2023 XiNGRZ
# SPDX-License-Identifier: MIT

# handler - fn-state

zephyr_library_sources(handler_fn_state.c)

# handler - layer index / caps toggle (knob haptic feedback)

zephyr_library_sources(handler_layer_index.c)
zephyr_library_sources(handler_caps_toggled.c)

zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
