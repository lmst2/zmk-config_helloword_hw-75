/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"

/*
 * Legacy: keyboard UART used to force dynamic to layer 1 while FN was held.
 * hw75_dynamic keymap layer 1 is "scroll" (OLED mode), not an FN mirror — wrong UX.
 * Keyboard FN is for RGB/media only; ignore FN_STATE from UART.
 */
bool handle_fn_state(const uart_comm_MessageK2D *k2d)
{
	(void)k2d;
	return true;
}
