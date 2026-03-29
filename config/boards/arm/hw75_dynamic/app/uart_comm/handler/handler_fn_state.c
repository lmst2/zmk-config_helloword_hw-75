/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <zmk/keymap.h>

static uint8_t saved_layer;

bool handle_fn_state(const uart_comm_MessageK2D *k2d)
{
	const uart_comm_FnState *report = &k2d->payload.fn_state;

	if (report->pressed) {
		saved_layer = zmk_keymap_highest_layer_active();
		zmk_keymap_layer_to(CONFIG_HW75_DYNAMIC_UART_FN_TARGET_LAYER);
	} else {
		zmk_keymap_layer_to(saved_layer);
	}

	return true;
}
