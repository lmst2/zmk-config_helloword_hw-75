/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <zephyr/device.h>
#include <knob/drivers/knob.h>
#include <app/indicator.h>
#include <eink_mode.h>

static const struct device *knob = DEVICE_DT_GET(DT_ALIAS(knob));
static bool indicator_on = true;

bool handle_touchbar_gesture(const uart_comm_MessageK2D *k2d)
{
	/* verb: enum hw75_touchbar_gesture (0 swipe_l, 1 swipe_r, 2 tap, 3 long).
	 * The keyboard's TouchBar drives the dynamic module's outputs, no PC. */
	switch (k2d->payload.touchbar_gesture.verb) {
	case 0: /* swipe left  -> previous e-ink mode */
		eink_mode_cycle(-1);
		break;
	case 1: /* swipe right -> next e-ink mode */
		eink_mode_cycle(1);
		break;
	case 2: /* tap -> toggle the 4 accent LEDs */
		indicator_on = !indicator_on;
		indicator_set_enable(indicator_on);
		break;
	case 3: /* long -> firm haptic confirmation bump */
		knob_pulse(knob, 80, 2);
		break;
	default:
		break;
	}

	return true;
}
