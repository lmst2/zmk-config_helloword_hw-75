/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW75_DIAG_BOOT_EVENT_LIMIT     CONFIG_HW75_DIAG_LOG_BOOT_EVENT_COUNT
#define HW75_DIAG_LOG_EVENT_BATCH_MAX  4

enum hw75_diag_level {
	HW75_DIAG_LEVEL_ERROR = 0,
	HW75_DIAG_LEVEL_WARN = 1,
	HW75_DIAG_LEVEL_INFO = 2,
	HW75_DIAG_LEVEL_DEBUG = 3,
	HW75_DIAG_LEVEL_TRACE = 4,
};

enum hw75_diag_module {
	HW75_DIAG_MODULE_SYSTEM = 0,
	HW75_DIAG_MODULE_USB_COMM = 1,
	HW75_DIAG_MODULE_KSCAN = 2,
	HW75_DIAG_MODULE_TOUCHBAR = 3,
	HW75_DIAG_MODULE_BEHAVIOR = 4,
	HW75_DIAG_MODULE_HID = 5,
	HW75_DIAG_MODULE_RGB = 6,
	HW75_DIAG_MODULE_INDICATOR = 7,
	HW75_DIAG_MODULE_SETTINGS = 8,
	HW75_DIAG_MODULE_EINK = 9,
	HW75_DIAG_MODULE_KNOB = 10,
	HW75_DIAG_MODULE_HELPER_CORE = 11,
};

enum hw75_diag_event_id {
	HW75_DIAG_EVENT_SYSTEM_INIT = 1,
	HW75_DIAG_EVENT_SYSTEM_LOG_CONFIG = 2,
	HW75_DIAG_EVENT_USB_INIT = 10,
	HW75_DIAG_EVENT_USB_REQUEST = 11,
	HW75_DIAG_EVENT_USB_RX_OVERFLOW = 12,
	HW75_DIAG_EVENT_USB_PACKET_HEADER_INVALID = 13,
	HW75_DIAG_EVENT_USB_DECODE_FAIL = 14,
	HW75_DIAG_EVENT_USB_ENCODE_FAIL = 15,
	HW75_DIAG_EVENT_USB_RESPONSE_LARGE = 16,
	HW75_DIAG_EVENT_USB_BYTES_OVERFLOW = 17,
	HW75_DIAG_EVENT_USB_BYTES_DECODE_FAIL = 18,
	HW75_DIAG_EVENT_USB_TX_WAIT_TIMEOUT = 19,
	HW75_DIAG_EVENT_USB_TX_WRITE_FAIL = 20,
	HW75_DIAG_EVENT_USB_TX_SHORT_WRITE = 21,
	HW75_DIAG_EVENT_KSCAN_TOUCH_MAP = 30,
	HW75_DIAG_EVENT_KSCAN_TOUCH_STATE = 31,
	HW75_DIAG_EVENT_BEHAVIOR_TOUCHBAR_MODE = 40,
	HW75_DIAG_EVENT_HID_INIT = 50,
	HW75_DIAG_EVENT_HID_INIT_FAIL = 51,
	HW75_DIAG_EVENT_HID_WHEEL_SEND = 52,
	HW75_DIAG_EVENT_HID_WHEEL_SEND_FAIL = 53,
	HW75_DIAG_EVENT_RGB_CONTROL = 60,
	HW75_DIAG_EVENT_RGB_SET_STATE = 61,
	HW75_DIAG_EVENT_RGB_SET_INDICATOR = 62,
	HW75_DIAG_EVENT_INDICATOR_INIT = 70,
	HW75_DIAG_EVENT_INDICATOR_ENABLE = 71,
	HW75_DIAG_EVENT_INDICATOR_BRIGHTNESS_ACTIVE = 72,
	HW75_DIAG_EVENT_INDICATOR_BRIGHTNESS_INACTIVE = 73,
	HW75_DIAG_EVENT_TOUCHBAR_INIT = 80,
	HW75_DIAG_EVENT_TOUCHBAR_INIT_FAIL = 81,
	HW75_DIAG_EVENT_TOUCHBAR_POLL_ERROR = 82,
	HW75_DIAG_EVENT_TOUCHBAR_POLL_RECOVERED = 83,
	HW75_DIAG_EVENT_TOUCHBAR_MODE_CYCLE = 84,
	HW75_DIAG_EVENT_TOUCHBAR_TOUCH_START = 85,
	HW75_DIAG_EVENT_TOUCHBAR_ACTIVATION_WAIT = 86,
	HW75_DIAG_EVENT_TOUCHBAR_GESTURE_ACTIVE = 87,
	HW75_DIAG_EVENT_TOUCHBAR_PAN_WHEEL = 88,
	HW75_DIAG_EVENT_TOUCHBAR_APP_SWITCH_STEP = 89,
	HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_SWITCH_STEP = 90,
	HW75_DIAG_EVENT_TOUCHBAR_EDGE_HOLD_ARM = 91,
	HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_EDGE_ARM = 92,
	HW75_DIAG_EVENT_TOUCHBAR_APP_RELEASE_GUARD = 93,
	HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_FINALIZE = 94,
	HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_HOLD_WAIT = 95,
	HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_SEEK = 96,
	HW75_DIAG_EVENT_TOUCHBAR_RELEASE_PENDING = 97,
	HW75_DIAG_EVENT_TOUCHBAR_RELEASE_GRACE = 98,
	HW75_DIAG_EVENT_TOUCHBAR_TOUCH_END = 99,
	HW75_DIAG_EVENT_TOUCHBAR_APP_RELEASE_JITTER = 100,
	HW75_DIAG_EVENT_USB_STACK_WATERMARK = 110,
	HW75_DIAG_EVENT_RGB_WORKQ_STACK_WATERMARK = 111,
	HW75_DIAG_EVENT_EINK_MODE_CHANGED = 120,
	HW75_DIAG_EVENT_EINK_FRAME_RECEIVED = 121,
	HW75_DIAG_EVENT_EINK_RENDER = 122,
	HW75_DIAG_EVENT_KNOB_ZERO_OFFSET_APPLIED = 130,
	HW75_DIAG_EVENT_HELPER_CLOCK_SYNC = 140,
	HW75_DIAG_EVENT_HELPER_WEATHER_SYNC = 141,
};

enum hw75_diag_touchbar_phase {
	HW75_DIAG_TOUCHBAR_PHASE_INIT = 0,
	HW75_DIAG_TOUCHBAR_PHASE_MODE_CYCLE = 1,
	HW75_DIAG_TOUCHBAR_PHASE_TOUCH_START = 2,
	HW75_DIAG_TOUCHBAR_PHASE_ACTIVE = 3,
	HW75_DIAG_TOUCHBAR_PHASE_RELEASED = 4,
};

struct hw75_diag_config {
	bool has_min_level;
	enum hw75_diag_level min_level;
	bool has_enabled_modules_mask;
	uint32_t enabled_modules_mask;
};

struct hw75_diag_event {
	uint32_t seq;
	uint32_t uptime_ms;
	enum hw75_diag_level level;
	enum hw75_diag_module module;
	enum hw75_diag_event_id event_id;
	uint32_t trace_id;
	uint32_t data0;
	uint32_t data1;
	uint32_t repeat_count;
	bool has_trace_id;
	bool has_data1;
};

struct hw75_diag_snapshot {
	bool valid;
	enum hw75_diag_module module;
	uint32_t updated_ms;
	uint32_t state0;
	uint32_t state1;
	uint32_t state2;
};

struct hw75_diag_config_view {
	enum hw75_diag_level min_level;
	uint32_t enabled_modules_mask;
	uint32_t dropped_count;
	uint32_t oldest_seq;
	uint32_t newest_seq;
};

static inline uint32_t hw75_diag_pack_u8x4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

static inline uint32_t hw75_diag_pack_u16x2(uint16_t lo, uint16_t hi) {
	return (uint32_t)lo | ((uint32_t)hi << 16);
}

static inline uint32_t hw75_diag_pack_s16x2(int16_t lo, int16_t hi) {
	return hw75_diag_pack_u16x2((uint16_t)lo, (uint16_t)hi);
}

static inline uint32_t hw75_diag_pack_bool_u8x3(bool a, uint8_t b, uint8_t c, uint8_t d) {
	return hw75_diag_pack_u8x4(a ? 1U : 0U, b, c, d);
}

uint32_t hw75_diag_next_trace_id(void);

void hw75_diag_log_event(enum hw75_diag_level level, enum hw75_diag_module module,
			 uint32_t trace_id, enum hw75_diag_event_id event_id, uint32_t data0,
			 bool has_data1, uint32_t data1);

void hw75_diag_update_snapshot(enum hw75_diag_module module, uint32_t state0, uint32_t state1,
			       uint32_t state2);

void hw75_diag_set_config(const struct hw75_diag_config *config);
void hw75_diag_clear_events(void);

void hw75_diag_get_config_view(struct hw75_diag_config_view *view);

size_t hw75_diag_get_snapshot_count(void);
bool hw75_diag_copy_snapshot_at(size_t index, struct hw75_diag_snapshot *dst);
size_t hw75_diag_get_boot_event_count(void);
bool hw75_diag_copy_boot_event_at(size_t index, struct hw75_diag_event *dst);
bool hw75_diag_copy_event_after_at(uint32_t after_seq, size_t index, struct hw75_diag_event *dst);

#ifdef __cplusplus
}
#endif
