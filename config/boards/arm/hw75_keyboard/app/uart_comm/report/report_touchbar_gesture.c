/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <app/touchbar.h>

#include "report.h"

/*
 * Strong implementation of the weak hook in touchbar.c: when the strip is in
 * REMOTE mode, forward the completed gesture to the dynamic module over UART
 * instead of emitting HID. Edge-triggered (once per gesture), so it fits the
 * uart_slip budget comfortably.
 */
void hw75_touchbar_remote_gesture(uint8_t verb)
{
	uart_comm_MessageK2D k2d = uart_comm_MessageK2D_init_zero;
	k2d.action = uart_comm_Action_TOUCHBAR_GESTURE;
	k2d.which_payload = uart_comm_MessageK2D_touchbar_gesture_tag;
	k2d.payload.touchbar_gesture.verb = verb;

	uart_comm_report(&k2d);
}
