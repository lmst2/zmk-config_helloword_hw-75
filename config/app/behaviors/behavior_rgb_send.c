/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgb_send

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

/*
 * Defined in hw75_dynamic/app/uart_comm/uart_comm.c. Sends a D2K_RGB opcode to
 * the keyboard over the reverse UART; the keyboard maps it to its underglow
 * (1 toggle, 2/3 bri±, 4/5 eff±, 6/7 hue±, 8/9 sat±, 10/11 spd±). This lets the
 * dynamic module's knob/keys configure the keyboard RGB with no host — the
 * keyboard stays stateless.
 */
extern void uart_comm_send_rgb_cmd(uint32_t command);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(event);
	uart_comm_send_rgb_cmd((uint32_t)binding->param1);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_rgb_send_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int behavior_rgb_send_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_rgb_send_init, NULL, NULL, NULL, APPLICATION,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_rgb_send_driver_api);

#endif
