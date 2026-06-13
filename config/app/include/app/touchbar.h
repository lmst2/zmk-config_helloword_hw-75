/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <app/kscan_74hc165.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW75_TOUCHBAR_MAX_SEGMENT_COUNT HW75_TOUCHBAR_CHANNEL_COUNT
#define HW75_TOUCHBAR_MODE_COUNT 3U

enum hw75_touchbar_mode {
	HW75_TOUCHBAR_MODE_PAN = 0,
	HW75_TOUCHBAR_MODE_APP_SWITCH = 1,
	HW75_TOUCHBAR_MODE_DESKTOP_SWITCH = 2,
};

struct hw75_touchbar_mode_indicator {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint16_t duration_ms;
};

struct hw75_touchbar_pan_config {
	uint16_t activation_ms;
	uint16_t release_grace_ms;
	uint16_t poll_interval_ms;
	uint16_t interval_ms;
	uint16_t deadzone;
	uint16_t position_scale;
};

struct hw75_touchbar_app_switch_config {
	uint16_t activation_ms;
	uint16_t release_grace_ms;
	uint16_t release_settle_ms;
	uint16_t step_interval_ms;
	uint16_t step_distance;
	uint16_t edge_repeat_delay_ms;
};

struct hw75_touchbar_desktop_switch_config {
	uint16_t activation_ms;
	uint16_t release_grace_ms;
	uint16_t hold_ms;
	uint16_t step_interval_ms;
	uint16_t step_distance;
	uint16_t edge_repeat_delay_ms;
	uint16_t swipe_distance;
};

struct hw75_touchbar_config_view {
	enum hw75_touchbar_mode mode;
	uint8_t logical_point_count;
	uint8_t segment_count;
	uint8_t shared_point_count;
	uint8_t segment_touch_masks[HW75_TOUCHBAR_MAX_SEGMENT_COUNT];
	uint8_t segment_entry_masks[HW75_TOUCHBAR_MAX_SEGMENT_COUNT];
	uint8_t logical_rows[HW75_TOUCHBAR_CHANNEL_COUNT];
	uint8_t logical_cols[HW75_TOUCHBAR_CHANNEL_COUNT];
	bool mode_indicator_enabled;
	struct hw75_touchbar_mode_indicator mode_indicators[HW75_TOUCHBAR_MODE_COUNT];
	struct hw75_touchbar_pan_config pan;
	struct hw75_touchbar_app_switch_config app_switch;
	struct hw75_touchbar_desktop_switch_config desktop_switch;
};

int touchbar_cycle_mode(void);
int touchbar_set_mode(enum hw75_touchbar_mode mode);
enum hw75_touchbar_mode touchbar_get_mode(void);
int touchbar_get_config_view(struct hw75_touchbar_config_view *view);
int touchbar_set_config_view(const struct hw75_touchbar_config_view *view);

#ifdef __cplusplus
}
#endif
