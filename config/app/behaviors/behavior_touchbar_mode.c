/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_touchbar_mode

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <app/diag_log.h>
#include <app/touchbar.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);

	uint32_t trace_id = hw75_diag_next_trace_id();
	hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_BEHAVIOR, trace_id,
			    HW75_DIAG_EVENT_BEHAVIOR_TOUCHBAR_MODE, 0U, false, 0U);
	return touchbar_cycle_mode();
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);

	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_touchbar_mode_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int behavior_touchbar_mode_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_touchbar_mode_init, NULL, NULL, NULL, APPLICATION,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_touchbar_mode_driver_api);

#endif
