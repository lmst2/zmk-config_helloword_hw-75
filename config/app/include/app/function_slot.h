/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW75_FUNCTION_SLOT_COUNT 5U
#define HW75_FUNCTION_SLOT_MAX_MACRO_STEPS 6U
#define HW75_FUNCTION_SLOT_EVENT_BATCH_MAX 4U
#define HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE 4U

enum hw75_function_slot_type {
	HW75_FUNCTION_SLOT_TYPE_NONE = 0,
	HW75_FUNCTION_SLOT_TYPE_HID_PRESET = 1,
	HW75_FUNCTION_SLOT_TYPE_KEY_COMBO = 2,
	HW75_FUNCTION_SLOT_TYPE_MACRO_SEQ = 3,
	HW75_FUNCTION_SLOT_TYPE_HELPER_ACTION = 4,
};

enum hw75_function_slot_hid_preset {
	HW75_FUNCTION_SLOT_HID_PRESET_NONE = 0,
	HW75_FUNCTION_SLOT_HID_PRESET_MUTE = 1,
	HW75_FUNCTION_SLOT_HID_PRESET_VOL_UP = 2,
	HW75_FUNCTION_SLOT_HID_PRESET_VOL_DOWN = 3,
	HW75_FUNCTION_SLOT_HID_PRESET_PLAY_PAUSE = 4,
	HW75_FUNCTION_SLOT_HID_PRESET_PREV = 5,
	HW75_FUNCTION_SLOT_HID_PRESET_NEXT = 6,
	HW75_FUNCTION_SLOT_HID_PRESET_STOP = 7,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_SLEEP = 8,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_WAKE = 9,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_POWER = 10,
	HW75_FUNCTION_SLOT_HID_PRESET_CALCULATOR = 11,
	HW75_FUNCTION_SLOT_HID_PRESET_MAIL = 12,
	HW75_FUNCTION_SLOT_HID_PRESET_BROWSER = 13,
	HW75_FUNCTION_SLOT_HID_PRESET_FILE_BROWSER = 14,
	HW75_FUNCTION_SLOT_HID_PRESET_IMAGE_BROWSER = 15,
	HW75_FUNCTION_SLOT_HID_PRESET_MUSIC_BROWSER = 16,
};

enum hw75_function_slot_macro_step_type {
	HW75_FUNCTION_SLOT_MACRO_STEP_TAP = 0,
	HW75_FUNCTION_SLOT_MACRO_STEP_DOWN = 1,
	HW75_FUNCTION_SLOT_MACRO_STEP_UP = 2,
	HW75_FUNCTION_SLOT_MACRO_STEP_DELAY = 3,
};

struct hw75_function_slot_macro_step {
	uint8_t type;
	uint8_t modifiers;
	uint8_t usage_id;
	uint8_t delay_ms;
};

struct hw75_function_slot_config {
	uint8_t slot_index;
	uint8_t slot_type;
	uint8_t flags;
	uint16_t action_code;
	int16_t arg0;
	int16_t arg1;
	int16_t arg2;
	uint8_t macro_step_count;
	struct hw75_function_slot_macro_step macro_steps[HW75_FUNCTION_SLOT_MAX_MACRO_STEPS];
	uint8_t checksum;
};

struct hw75_function_slot_caps {
	uint8_t slot_count;
	uint8_t max_macro_steps;
	uint8_t supported_hid_preset_count;
	uint16_t supported_hid_presets[16];
};

struct hw75_function_slot_event {
	uint16_t seq;
	uint8_t slot_index;
	uint16_t action_code;
	int16_t arg0;
	int16_t arg1;
	int16_t arg2;
	uint8_t flags;
};

struct hw75_function_slot_event_batch {
	uint8_t count;
	uint16_t dropped_count;
	uint16_t oldest_seq;
	uint16_t newest_seq;
	struct hw75_function_slot_event events[HW75_FUNCTION_SLOT_EVENT_BATCH_MAX];
};

int hw75_function_slot_get_caps(struct hw75_function_slot_caps *caps);
int hw75_function_slot_get_config(uint8_t slot_index, struct hw75_function_slot_config *config);
int hw75_function_slot_set_config(const struct hw75_function_slot_config *config);
int hw75_function_slot_binding_pressed(uint8_t slot_index, int64_t timestamp);
int hw75_function_slot_binding_released(uint8_t slot_index, int64_t timestamp);
int hw75_function_slot_fetch_events(uint16_t after_seq, uint8_t max_count,
				    struct hw75_function_slot_event_batch *batch);

#ifdef __cplusplus
}
#endif
