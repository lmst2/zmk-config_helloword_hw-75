/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

#include "report.h"

static uint8_t last_index = 0xFF;

static int report_layer_index_listener(const zmk_event_t *eh)
{
	if (as_zmk_layer_state_changed(eh)) {
		uint8_t index = zmk_keymap_highest_layer_active();
		if (index != last_index) {
			last_index = index;

			uart_comm_MessageK2D k2d = uart_comm_MessageK2D_init_zero;
			k2d.action = uart_comm_Action_LAYER_INDEX;
			k2d.which_payload = uart_comm_MessageK2D_layer_index_tag;
			k2d.payload.layer_index.index = index;

			uart_comm_report(&k2d);
		}
		return 0;
	}

	return -ENOTSUP;
}

ZMK_LISTENER(report_layer_index, report_layer_index_listener);
ZMK_SUBSCRIPTION(report_layer_index, zmk_layer_state_changed);
