/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <knob/drivers/knob.h> /* enum knob_mode, used by struct knob_pref in knob_app.h */
#include <knob_app.h>

bool handle_activity(const uart_comm_MessageK2D *k2d)
{
	const uart_comm_ActivityHint *report = &k2d->payload.activity_hint;
	bool active = report->state == 0;

	/* state 0 = active. Keep the knob (and thus the module) awake while the
	 * keyboard is in use, fixing the case where the dynamic idled itself
	 * because the knob hadn't been touched even though you were typing. */
	knob_app_set_external_active(active);

	/*
	 * Activity-reactive underglow: dim the keyboard's own RGB one step when it
	 * goes idle/asleep, restore it on the next use — entirely over D2K, no host.
	 * One step per edge keeps it reversible and well within the non-blocking
	 * SLIP budget (multiple frames back-to-back could be dropped).
	 */
	static bool dimmed;
	if (!active && !dimmed) {
		uart_comm_send_rgb_cmd(3); /* brightness - */
		dimmed = true;
	} else if (active && dimmed) {
		uart_comm_send_rgb_cmd(2); /* brightness + */
		dimmed = false;
	}

	return true;
}
