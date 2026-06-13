/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <zephyr/device.h>
#include <knob/drivers/knob.h>

static const struct device *knob = DEVICE_DT_GET(DT_ALIAS(knob));

bool handle_caps_toggled(const uart_comm_MessageK2D *k2d)
{
	const uart_comm_CapsToggled *report = &k2d->payload.caps_toggled;

	/* Two firm bumps when Caps Lock engages, one softer bump when it
	 * releases — a glance-free tactile confirmation. */
	knob_pulse(knob, report->engaged ? 75 : 45, report->engaged ? 2 : 1);

	return true;
}
