/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_tb_send

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

/*
 * Cycle the keyboard TouchBar mode from the dynamic knob, with a haptic bump to
 * confirm. The keyboard owns the real mode (touchbar_set_mode); here we keep a
 * local 0..2 cursor (skipping host-only REMOTE) and send it absolute over D2K.
 * param1 is the signed step (+1 / -1). Defined in the dynamic uart_comm/knob_app.
 */
extern void uart_comm_send_tb_mode(uint32_t mode);
extern void knob_app_pulse(void);
extern void eink_mode_toast_touchbar(uint8_t mode);

#define TB_MODE_COUNT 3

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int g_tb_mode;

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(event);
	int delta = ((int32_t)binding->param1 < 0) ? -1 : 1;
	g_tb_mode = (g_tb_mode + delta + TB_MODE_COUNT) % TB_MODE_COUNT;
	uart_comm_send_tb_mode((uint32_t)g_tb_mode);
	knob_app_pulse();
#if IS_ENABLED(CONFIG_HW75_EINK_MODES)
	eink_mode_toast_touchbar((uint8_t)g_tb_mode); /* centered e-ink mode toast */
#endif
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tb_send_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int behavior_tb_send_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_tb_send_init, NULL, NULL, NULL, APPLICATION,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tb_send_driver_api);

#endif
