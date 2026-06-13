# Copyright (c) 2022-2023 XiNGRZ
# SPDX-License-Identifier: MIT

# handler - fn-state

zephyr_library_sources(handler_fn_state.c)

# handler - layer index / caps toggle (knob haptic feedback)

zephyr_library_sources(handler_layer_index.c)
zephyr_library_sources(handler_caps_toggled.c)

# handler - activity (keyboard typing keeps the knob awake)

zephyr_library_sources(handler_activity.c)

# handler - touchbar gesture (REMOTE mode drives e-ink / RGB / knob)

zephyr_library_sources(handler_touchbar_gesture.c)

zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
# Reach the dynamic board's app/ headers (knob_app.h, eink_mode.h, ...).
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
