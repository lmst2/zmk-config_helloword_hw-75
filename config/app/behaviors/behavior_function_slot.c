/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_function_slot

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <app/function_slot.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event) {
	return hw75_function_slot_binding_pressed((uint8_t)binding->param1, event.timestamp);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event) {
	return hw75_function_slot_binding_released((uint8_t)binding->param1, event.timestamp);
}

static const struct behavior_driver_api behavior_function_slot_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int behavior_function_slot_init(const struct device *dev) {
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_function_slot_init, NULL, NULL, NULL, APPLICATION,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_function_slot_driver_api);

#endif
