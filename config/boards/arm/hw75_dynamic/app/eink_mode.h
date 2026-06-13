/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep in sync with usb_comm.proto::EinkModeType and frontend enum. */
enum eink_mode_type {
	EINK_MODE_TYPE_OFF = 0,
	EINK_MODE_TYPE_STATIC = 1,
	EINK_MODE_TYPE_SLIDESHOW = 2,
	EINK_MODE_TYPE_CLOCK_WEATHER = 3,
};

#define EINK_MODE_CAPACITY 8
#define EINK_FRAME_CAPACITY 8
#define EINK_MODE_LABEL_LEN 24

struct eink_mode_entry {
	uint8_t id;
	enum eink_mode_type type;
	uint32_t refresh_interval_s;
	uint8_t frame_count;
	char label[EINK_MODE_LABEL_LEN];
};

struct eink_mode_view {
	uint8_t count;
	uint8_t active_index;
	struct eink_mode_entry modes[EINK_MODE_CAPACITY];
};

/* Bitmap payload for one full-screen frame (128 x 296 / 8). */
#define EINK_FRAME_BYTES (128u * 296u / 8u)

void eink_mode_get_view(struct eink_mode_view *view);
int eink_mode_set_config(const struct eink_mode_entry *modes, uint8_t count, uint8_t active_index);
int eink_mode_set_active(uint8_t active_index);
int eink_mode_cycle(int delta);

/*
 * Make a host-pushed full frame (EINK_SET_IMAGE) the active on-screen view. It
 * stays put — the configured mode stops drawing, so the autonomous clock/weather
 * tick can't overwrite it (the clock still advances internally). Released by
 * eink_mode_set_active() / eink_mode_set_config(), which resume that mode.
 */
int eink_mode_show_external(const uint8_t *bits, uint32_t bits_len, bool partial);

/* Pop a centered TouchBar-mode toast for ~0.8 s, then restore the panel. Floats
 * over the active view (clock/weather or external image) via partial refresh. */
void eink_mode_toast_touchbar(uint8_t mode);

int eink_mode_push_frame(uint8_t mode_id, uint8_t frame_index, const uint8_t *bits,
			 uint32_t bits_len);
int eink_mode_push_clock(uint8_t hour, uint8_t minute, uint8_t day, uint8_t month, uint8_t weekday,
			 uint16_t year);
int eink_mode_push_weather(int16_t temp_deci_c, uint8_t icon_id, const char *city);

uint8_t eink_mode_get_active_index(void);
uint8_t eink_mode_get_active_id(void);
enum eink_mode_type eink_mode_get_active_type(void);
