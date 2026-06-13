/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "uart_comm.pb.h"

typedef bool (*uart_comm_handler_t)(const uart_comm_MessageK2D *k2d);

bool handle_fn_state(const uart_comm_MessageK2D *k2d);
bool handle_layer_index(const uart_comm_MessageK2D *k2d);
bool handle_caps_toggled(const uart_comm_MessageK2D *k2d);
bool handle_activity(const uart_comm_MessageK2D *k2d);
bool handle_touchbar_gesture(const uart_comm_MessageK2D *k2d);

/* Reverse channel: dynamic -> keyboard (F6). command: 1 toggle, 2 bri+, 3 bri-,
 * 4 eff+, 5 eff-. Drives the keyboard's own RGB underglow over the D2K link. */
void uart_comm_send_rgb_cmd(uint32_t command);
