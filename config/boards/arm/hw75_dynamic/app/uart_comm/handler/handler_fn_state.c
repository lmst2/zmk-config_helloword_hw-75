/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <zmk/keymap.h>

static uint8_t current_layer = 0;

bool handle_fn_state(const uart_comm_MessageK2D *k2d)
{
	const uart_comm_FnState *report = &k2d->payload.fn_state;

	if (report->pressed) {
		current_layer = zmk_keymap_highest_layer_active();
		zmk_keymap_layer_to(1);
		/* Demonstrate the reverse channel end-to-end: tapping FN on the
		 * keyboard makes the dynamic drive the keyboard's own RGB effect
		 * over the D2K link (rebind to anything; the transport is the point). */
		uart_comm_send_rgb_cmd(4); /* effect next */
	} else {
		zmk_keymap_layer_to(current_layer);
	}

	return true;
}
