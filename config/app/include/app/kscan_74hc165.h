/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define HW75_TOUCHBAR_CHANNEL_COUNT 6U

#ifdef __cplusplus
extern "C" {
#endif

int hw75_kscan_74hc165_get_touchbar_state(const struct device *dev, uint8_t *state,
					  int64_t *timestamp);
void hw75_kscan_74hc165_get_touchbar_logical_map(uint8_t rows[HW75_TOUCHBAR_CHANNEL_COUNT],
						 uint8_t cols[HW75_TOUCHBAR_CHANNEL_COUNT]);

#ifdef __cplusplus
}
#endif
