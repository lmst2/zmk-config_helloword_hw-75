/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

#include <zephyr/device.h>
#include <knob/drivers/knob.h>

static const struct device *knob = DEVICE_DT_GET(DT_ALIAS(knob));

bool handle_layer_index(const uart_comm_MessageK2D *k2d)
{
	ARG_UNUSED(k2d);

	/* One crisp bump confirms a keyboard layer change in the fingertips,
	 * with no PC involved. The knob's per-layer profile is applied
	 * separately via the FN_STATE path. */
	knob_pulse(knob, 45, 1);

	return true;
}
