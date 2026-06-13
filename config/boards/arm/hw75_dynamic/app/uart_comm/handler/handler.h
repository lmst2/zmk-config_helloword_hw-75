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

/* Reverse channel: dynamic -> keyboard (D2K). Drive the keyboard's own RGB
 * underglow (opcodes 1-11: toggle / bri± / eff± / hue± / sat± / spd±) and pick
 * its TouchBar mode (0 PAN, 1 APP_SWITCH, 2 DESKTOP_SWITCH) with no host. */
void uart_comm_send_rgb_cmd(uint32_t command);
void uart_comm_send_tb_mode(uint32_t mode);
