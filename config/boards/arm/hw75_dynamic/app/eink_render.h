/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EINK_RENDER_WIDTH 128u
#define EINK_RENDER_HEIGHT 296u
#define EINK_RENDER_FRAME_BYTES (EINK_RENDER_WIDTH * EINK_RENDER_HEIGHT / 8u)

enum eink_weather_icon {
	EINK_WEATHER_UNKNOWN = 0,
	EINK_WEATHER_SUNNY = 1,
	EINK_WEATHER_CLOUDY = 2,
	EINK_WEATHER_OVERCAST = 3,
	EINK_WEATHER_RAINY = 4,
	EINK_WEATHER_SNOWY = 5,
	EINK_WEATHER_STORM = 6,
	EINK_WEATHER_FOGGY = 7,
	EINK_WEATHER_NIGHT = 8,
	EINK_WEATHER_ICON_COUNT,
};

struct eink_render_clock {
	uint8_t hour;
	uint8_t minute;
	uint8_t day;
	uint8_t month;
	uint8_t weekday; /* 0 = Sunday */
	uint16_t year;
	bool has_date;
	bool valid;
};

struct eink_render_weather {
	int16_t temp_deci_c;
	uint8_t icon;
	char city[24];
	bool valid;
};

/* Fill buf with a white background (0xFF bytes). */
void eink_render_clear(uint8_t *buf);

/* Render the current clock+weather state into buf. */
void eink_render_clock_weather(uint8_t *buf, const struct eink_render_clock *clock,
			       const struct eink_render_weather *weather);
