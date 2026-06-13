/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include "report.h"

/*
 * Local Caps Lock guess: this ZMK fork has no host hid_indicators event, so we
 * cannot know the real Caps LED state. We approximate by toggling on each local
 * CAPSLOCK key press, which is correct as long as the host stays in sync.
 */
static bool caps_engaged = false;

static int report_caps_listener(const zmk_event_t *eh)
{
	const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
	if (ev == NULL) {
		return -ENOTSUP;
	}

	if (ev->state && ev->usage_page == HID_USAGE_KEY &&
	    ev->keycode == HID_USAGE_KEY_KEYBOARD_CAPS_LOCK) {
		caps_engaged = !caps_engaged;

		uart_comm_MessageK2D k2d = uart_comm_MessageK2D_init_zero;
		k2d.action = uart_comm_Action_CAPS_TOGGLED;
		k2d.which_payload = uart_comm_MessageK2D_caps_toggled_tag;
		k2d.payload.caps_toggled.engaged = caps_engaged;

		uart_comm_report(&k2d);
	}

	return 0;
}

ZMK_LISTENER(report_caps, report_caps_listener);
ZMK_SUBSCRIPTION(report_caps, zmk_keycode_state_changed);
