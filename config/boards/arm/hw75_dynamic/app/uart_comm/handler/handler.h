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
