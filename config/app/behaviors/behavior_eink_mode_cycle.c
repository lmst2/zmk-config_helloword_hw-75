/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_eink_mode_cycle

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <eink_mode.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(event);
	int delta = (int32_t)binding->param1;
	if (delta == 0) {
		delta = 1;
	}
	eink_mode_cycle(delta);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_eink_mode_cycle_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int behavior_eink_mode_cycle_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_eink_mode_cycle_init, NULL, NULL, NULL, APPLICATION,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_eink_mode_cycle_driver_api);

#endif
