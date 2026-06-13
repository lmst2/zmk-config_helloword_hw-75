/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#include "report.h"

static int report_activity_listener(const zmk_event_t *eh)
{
	if (as_zmk_activity_state_changed(eh)) {
		/* enum zmk_activity_state: ACTIVE=0, IDLE=1, SLEEP=2. */
		enum zmk_activity_state state = zmk_activity_get_state();

		uart_comm_MessageK2D k2d = uart_comm_MessageK2D_init_zero;
		k2d.action = uart_comm_Action_ACTIVITY_HINT;
		k2d.which_payload = uart_comm_MessageK2D_activity_hint_tag;
		k2d.payload.activity_hint.state = (uint32_t)state;

		uart_comm_report(&k2d);
		return 0;
	}

	return -ENOTSUP;
}

ZMK_LISTENER(report_activity, report_activity_listener);
ZMK_SUBSCRIPTION(report_activity, zmk_activity_state_changed);
