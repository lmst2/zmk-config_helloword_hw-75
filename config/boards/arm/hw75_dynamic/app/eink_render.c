/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "eink_render.h"

/*
 * The E-Ink framebuffer is 1 bit per pixel, MSB-first within each byte,
 * row-major with pitch = width/8 bytes. Bit value 1 means white; we clear
 * the buffer to 0xFF (white) and draw foreground strokes as zeros.
 */
#define PITCH (EINK_RENDER_WIDTH / 8u)
#define COLOR_WHITE 1
#define COLOR_BLACK 0

static inline void put_pixel(uint8_t *buf, int x, int y, int color)
{
	if (x < 0 || x >= (int)EINK_RENDER_WIDTH || y < 0 || y >= (int)EINK_RENDER_HEIGHT) {
		return;
	}
	uint32_t byte_index = (uint32_t)y * PITCH + ((uint32_t)x >> 3);
	uint8_t mask = 0x80u >> ((uint32_t)x & 0x07u);
	if (color) {
		buf[byte_index] |= mask;
	} else {
		buf[byte_index] &= (uint8_t)~mask;
	}
}

static void fill_rect(uint8_t *buf, int x, int y, int w, int h, int color)
{
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			put_pixel(buf, x + i, y + j, color);
		}
	}
}

static void draw_hline(uint8_t *buf, int x, int y, int len, int thickness, int color)
{
	if (thickness < 1) {
		thickness = 1;
	}
	for (int t = 0; t < thickness; t++) {
		for (int i = 0; i < len; i++) {
			put_pixel(buf, x + i, y + t, color);
		}
	}
}

static void draw_vline(uint8_t *buf, int x, int y, int len, int thickness, int color)
{
	if (thickness < 1) {
		thickness = 1;
	}
	for (int t = 0; t < thickness; t++) {
		for (int j = 0; j < len; j++) {
			put_pixel(buf, x + t, y + j, color);
		}
	}
}

static void draw_circle(uint8_t *buf, int cx, int cy, int r, int color)
{
	int x = r, y = 0, err = 0;
	while (x >= y) {
		put_pixel(buf, cx + x, cy + y, color);
		put_pixel(buf, cx + y, cy + x, color);
		put_pixel(buf, cx - y, cy + x, color);
		put_pixel(buf, cx - x, cy + y, color);
		put_pixel(buf, cx - x, cy - y, color);
		put_pixel(buf, cx - y, cy - x, color);
		put_pixel(buf, cx + y, cy - x, color);
		put_pixel(buf, cx + x, cy - y, color);
		y++;
		err += 1 + 2 * y;
		if (2 * (err - x) + 1 > 0) {
			x--;
			err += 1 - 2 * x;
		}
	}
}

static void fill_circle(uint8_t *buf, int cx, int cy, int r, int color)
{
	for (int j = -r; j <= r; j++) {
		for (int i = -r; i <= r; i++) {
			if (i * i + j * j <= r * r) {
				put_pixel(buf, cx + i, cy + j, color);
			}
		}
	}
}

static void draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, int color)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	while (1) {
		put_pixel(buf, x0, y0, color);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/*
 * 7-segment digits. 'w' and 'h' are bounding box, 't' is stroke thickness.
 * Segments: a top, b top-right, c bottom-right, d bottom, e bottom-left,
 * f top-left, g middle.
 */
static const uint8_t seven_seg_table[10] = {
	0x3F, /* 0: abcdef    */
	0x06, /* 1: bc        */
	0x5B, /* 2: abged     */
	0x4F, /* 3: abgcd     */
	0x66, /* 4: fgbc      */
	0x6D, /* 5: afgcd     */
	0x7D, /* 6: afgecd    */
	0x07, /* 7: abc       */
	0x7F, /* 8: abcdefg   */
	0x6F, /* 9: abcdfg    */
};

static void draw_7seg_digit(uint8_t *buf, int x, int y, int digit, int w, int h, int t)
{
	if (digit < 0 || digit > 9) {
		return;
	}
	uint8_t segs = seven_seg_table[digit];
	int mid_y = y + (h - t) / 2;

	if (segs & 0x01) { /* a top */
		draw_hline(buf, x + t, y, w - 2 * t, t, COLOR_BLACK);
	}
	if (segs & 0x02) { /* b top-right */
		draw_vline(buf, x + w - t, y + t, mid_y - y - t, t, COLOR_BLACK);
	}
	if (segs & 0x04) { /* c bottom-right */
		draw_vline(buf, x + w - t, mid_y + t, (y + h - t) - (mid_y + t), t, COLOR_BLACK);
	}
	if (segs & 0x08) { /* d bottom */
		draw_hline(buf, x + t, y + h - t, w - 2 * t, t, COLOR_BLACK);
	}
	if (segs & 0x10) { /* e bottom-left */
		draw_vline(buf, x, mid_y + t, (y + h - t) - (mid_y + t), t, COLOR_BLACK);
	}
	if (segs & 0x20) { /* f top-left */
		draw_vline(buf, x, y + t, mid_y - y - t, t, COLOR_BLACK);
	}
	if (segs & 0x40) { /* g middle */
		draw_hline(buf, x + t, mid_y, w - 2 * t, t, COLOR_BLACK);
	}
}

static void draw_colon(uint8_t *buf, int x, int y, int h, int dot)
{
	int off1 = h / 3;
	int off2 = 2 * h / 3;
	fill_rect(buf, x, y + off1 - dot / 2, dot, dot, COLOR_BLACK);
	fill_rect(buf, x, y + off2 - dot / 2, dot, dot, COLOR_BLACK);
}

static void draw_slash(uint8_t *buf, int x, int y, int h, int thick)
{
	draw_line(buf, x, y + h - 1, x + h / 2, y, COLOR_BLACK);
	if (thick > 1) {
		draw_line(buf, x + 1, y + h - 1, x + h / 2 + 1, y, COLOR_BLACK);
	}
}

static void draw_dot(uint8_t *buf, int x, int y, int size)
{
	fill_rect(buf, x, y, size, size, COLOR_BLACK);
}

/* Small uppercase "C" drawn as three strokes, bounding box w x h. */
static void draw_char_C(uint8_t *buf, int x, int y, int w, int h, int t)
{
	draw_hline(buf, x + t, y, w - t, t, COLOR_BLACK);
	draw_hline(buf, x + t, y + h - t, w - t, t, COLOR_BLACK);
	draw_vline(buf, x, y, h, t, COLOR_BLACK);
}

static void draw_char_degree(uint8_t *buf, int x, int y, int r, int t)
{
	draw_circle(buf, x + r, y + r, r, COLOR_BLACK);
	if (t > 1) {
		draw_circle(buf, x + r, y + r, r - 1, COLOR_BLACK);
	}
}

static int digit_block_width(int w, int spacing)
{
	return w + spacing;
}

static void draw_number_pair(uint8_t *buf, int x, int y, int value, int w, int h, int t,
			     int spacing)
{
	int tens = (value / 10) % 10;
	int ones = value % 10;
	draw_7seg_digit(buf, x, y, tens, w, h, t);
	draw_7seg_digit(buf, x + digit_block_width(w, spacing), y, ones, w, h, t);
}

/* Render "XX.X" (e.g. 23.5) using big-digit style and a small dot. */
static void draw_fixed_1dp(uint8_t *buf, int x, int y, int value_deci, int w, int h, int t,
			   int spacing)
{
	int sign = value_deci < 0 ? -1 : 1;
	int abs_val = sign * value_deci;
	int ones = (abs_val / 10) % 10;
	int tens = (abs_val / 100) % 10;
	int frac = abs_val % 10;

	int cursor = x;
	if (sign < 0) {
		draw_hline(buf, cursor + 2, y + h / 2, w - 4, t, COLOR_BLACK);
		cursor += digit_block_width(w, spacing);
	}
	draw_7seg_digit(buf, cursor, y, tens, w, h, t);
	cursor += digit_block_width(w, spacing);
	draw_7seg_digit(buf, cursor, y, ones, w, h, t);
	cursor += digit_block_width(w, spacing);
	draw_dot(buf, cursor, y + h - t * 2, t);
	cursor += t + spacing;
	draw_7seg_digit(buf, cursor, y, frac, w, h, t);
}

/*
 * Simple 32x32 icon primitives. Black strokes on transparent background;
 * the caller clears the buffer to white first.
 */
/* Integer-friendly unit directions scaled by 100. */
static const int8_t sun_ray_dirs[8][2] = {
	{ 100, 0 },   { 71, 71 },    { 0, 100 },   { -71, 71 },
	{ -100, 0 },  { -71, -71 },  { 0, -100 },  { 71, -71 },
};

static void draw_sun(uint8_t *buf, int cx, int cy, int r)
{
	fill_circle(buf, cx, cy, r, COLOR_BLACK);
	for (int i = 0; i < 8; i++) {
		int dx = sun_ray_dirs[i][0];
		int dy = sun_ray_dirs[i][1];
		int x0 = cx + ((r + 3) * dx) / 100;
		int y0 = cy + ((r + 3) * dy) / 100;
		int x1 = cx + ((r + 8) * dx) / 100;
		int y1 = cy + ((r + 8) * dy) / 100;
		draw_line(buf, x0, y0, x1, y1, COLOR_BLACK);
	}
}

static void draw_cloud(uint8_t *buf, int cx, int cy, bool filled)
{
	if (filled) {
		fill_circle(buf, cx - 10, cy + 3, 8, COLOR_BLACK);
		fill_circle(buf, cx + 10, cy + 2, 9, COLOR_BLACK);
		fill_circle(buf, cx, cy - 3, 10, COLOR_BLACK);
		fill_rect(buf, cx - 15, cy + 3, 30, 10, COLOR_BLACK);
	} else {
		draw_circle(buf, cx - 10, cy + 3, 8, COLOR_BLACK);
		draw_circle(buf, cx + 10, cy + 2, 9, COLOR_BLACK);
		draw_circle(buf, cx, cy - 3, 10, COLOR_BLACK);
	}
}

static void draw_rain(uint8_t *buf, int cx, int cy)
{
	draw_cloud(buf, cx, cy - 6, false);
	for (int i = -12; i <= 12; i += 8) {
		draw_line(buf, cx + i, cy + 6, cx + i - 4, cy + 16, COLOR_BLACK);
	}
}

static void draw_snow(uint8_t *buf, int cx, int cy)
{
	draw_cloud(buf, cx, cy - 6, false);
	for (int i = -10; i <= 10; i += 10) {
		int x = cx + i, y = cy + 10;
		draw_line(buf, x - 2, y, x + 2, y, COLOR_BLACK);
		draw_line(buf, x, y - 2, x, y + 2, COLOR_BLACK);
	}
}

static void draw_storm(uint8_t *buf, int cx, int cy)
{
	draw_cloud(buf, cx, cy - 6, true);
	draw_line(buf, cx, cy + 4, cx - 4, cy + 12, COLOR_BLACK);
	draw_line(buf, cx - 4, cy + 12, cx + 2, cy + 12, COLOR_BLACK);
	draw_line(buf, cx + 2, cy + 12, cx - 2, cy + 20, COLOR_BLACK);
}

static void draw_fog(uint8_t *buf, int cx, int cy)
{
	for (int i = 0; i < 5; i++) {
		int y = cy - 10 + i * 5;
		int len = 24 - (i & 1) * 4;
		draw_hline(buf, cx - len / 2, y, len, 2, COLOR_BLACK);
	}
}

static void draw_moon(uint8_t *buf, int cx, int cy, int r)
{
	fill_circle(buf, cx, cy, r, COLOR_BLACK);
	fill_circle(buf, cx + r / 2, cy - r / 3, r, COLOR_WHITE);
}

static void draw_weather_icon(uint8_t *buf, int cx, int cy, uint8_t icon)
{
	switch (icon) {
	case EINK_WEATHER_SUNNY:
		draw_sun(buf, cx, cy, 8);
		break;
	case EINK_WEATHER_CLOUDY:
		draw_sun(buf, cx - 10, cy - 10, 5);
		draw_cloud(buf, cx + 2, cy + 2, false);
		break;
	case EINK_WEATHER_OVERCAST:
		draw_cloud(buf, cx, cy, true);
		break;
	case EINK_WEATHER_RAINY:
		draw_rain(buf, cx, cy);
		break;
	case EINK_WEATHER_SNOWY:
		draw_snow(buf, cx, cy);
		break;
	case EINK_WEATHER_STORM:
		draw_storm(buf, cx, cy);
		break;
	case EINK_WEATHER_FOGGY:
		draw_fog(buf, cx, cy);
		break;
	case EINK_WEATHER_NIGHT:
		draw_moon(buf, cx, cy, 10);
		break;
	case EINK_WEATHER_UNKNOWN:
	default:
		draw_circle(buf, cx, cy, 12, COLOR_BLACK);
		draw_line(buf, cx - 3, cy - 3, cx + 3, cy + 3, COLOR_BLACK);
		draw_line(buf, cx + 3, cy - 3, cx - 3, cy + 3, COLOR_BLACK);
		break;
	}
}

void eink_render_clear(uint8_t *buf)
{
	memset(buf, 0xFF, EINK_RENDER_FRAME_BYTES);
}

static void render_clock(uint8_t *buf, const struct eink_render_clock *clock)
{
	/* HH:MM rendered with 7-seg, layout centred horizontally. */
	const int DIGIT_W = 22;
	const int DIGIT_H = 44;
	const int THICK = 4;
	const int SPACING = 4;
	const int COLON_W = 8;

	int total = 4 * DIGIT_W + 3 * SPACING + COLON_W;
	int x = ((int)EINK_RENDER_WIDTH - total) / 2;
	int y = 18;

	draw_number_pair(buf, x, y, clock->valid ? clock->hour : 0, DIGIT_W, DIGIT_H, THICK,
			 SPACING);
	x += 2 * DIGIT_W + SPACING * 2;
	draw_colon(buf, x, y, DIGIT_H, THICK);
	x += COLON_W + SPACING;
	draw_number_pair(buf, x, y, clock->valid ? clock->minute : 0, DIGIT_W, DIGIT_H, THICK,
			 SPACING);

	/* Date line: MM/DD below the clock. */
	if (clock->valid && clock->has_date) {
		const int SW = 14;
		const int SH = 22;
		const int ST = 3;
		const int SP = 3;
		int date_total = 4 * SW + 3 * SP + 6;
		int dx = ((int)EINK_RENDER_WIDTH - date_total) / 2;
		int dy = y + DIGIT_H + 10;
		draw_number_pair(buf, dx, dy, clock->month, SW, SH, ST, SP);
		dx += 2 * SW + SP * 2;
		draw_slash(buf, dx, dy, SH, ST);
		dx += 6 + SP;
		draw_number_pair(buf, dx, dy, clock->day, SW, SH, ST, SP);
	}
}

static void render_weather(uint8_t *buf, const struct eink_render_weather *weather)
{
	/* Weather block sits in lower half. */
	int icon_cx = (int)EINK_RENDER_WIDTH / 2;
	int icon_cy = 160;
	if (weather->valid) {
		draw_weather_icon(buf, icon_cx, icon_cy, weather->icon);
	} else {
		draw_weather_icon(buf, icon_cx, icon_cy, EINK_WEATHER_UNKNOWN);
	}

	/* Temperature "XX.X°C" */
	const int TW = 14;
	const int TH = 22;
	const int TT = 3;
	const int TSP = 3;

	int value = weather->valid ? weather->temp_deci_c : 0;
	int x = 6;
	int y = 210;

	draw_fixed_1dp(buf, x, y, value, TW, TH, TT, TSP);

	int after_number = x + 3 * (TW + TSP) + TT + TSP;
	draw_char_degree(buf, after_number + 2, y + 2, 4, 2);
	draw_char_C(buf, after_number + 14, y, TW, TH, TT);
}

void eink_render_clock_weather(uint8_t *buf, const struct eink_render_clock *clock,
			       const struct eink_render_weather *weather)
{
	eink_render_clear(buf);
	render_clock(buf, clock);

	/* Divider between clock and weather block. */
	draw_hline(buf, 10, 132, EINK_RENDER_WIDTH - 20, 2, COLOR_BLACK);

	render_weather(buf, weather);
}

/* 2px black rectangle outline. */
static void draw_rect(uint8_t *buf, int x, int y, int w, int h)
{
	draw_hline(buf, x, y, w, 2, COLOR_BLACK);
	draw_hline(buf, x, y + h - 2, w, 2, COLOR_BLACK);
	draw_vline(buf, x, y, h, 2, COLOR_BLACK);
	draw_vline(buf, x + w - 2, y, h, 2, COLOR_BLACK);
}

/*
 * Centered floating toast for a transient on-device notice (the TouchBar mode
 * just picked from the knob). A white card with a black border + a mode glyph,
 * drawn over whatever the panel was showing. The caller saves/restores the
 * underlying frame and partial-refreshes, so it pops in and clears fast without
 * disturbing the clock/weather (or now-playing card) behind it. The e-ink has
 * no on-device font, so the mode is shown as an icon, not text.
 */
void eink_render_touchbar_toast(uint8_t *buf, uint8_t mode)
{
	const int BW = 84;
	const int BH = 60;
	const int bx = ((int)EINK_RENDER_WIDTH - BW) / 2;
	const int by = ((int)EINK_RENDER_HEIGHT - BH) / 2;

	/* White card + 3px black border so it reads as a floating label. */
	fill_rect(buf, bx, by, BW, BH, COLOR_WHITE);
	draw_hline(buf, bx, by, BW, 3, COLOR_BLACK);
	draw_hline(buf, bx, by + BH - 3, BW, 3, COLOR_BLACK);
	draw_vline(buf, bx, by, BH, 3, COLOR_BLACK);
	draw_vline(buf, bx + BW - 3, by, BH, 3, COLOR_BLACK);

	const int cx = bx + BW / 2;
	const int cy = by + BH / 2;

	switch (mode) {
	case 0: /* PAN: horizontal double-headed arrow */
		draw_hline(buf, cx - 18, cy - 1, 36, 2, COLOR_BLACK);
		draw_line(buf, cx - 18, cy, cx - 10, cy - 8, COLOR_BLACK);
		draw_line(buf, cx - 18, cy, cx - 10, cy + 8, COLOR_BLACK);
		draw_line(buf, cx + 17, cy, cx + 9, cy - 8, COLOR_BLACK);
		draw_line(buf, cx + 17, cy, cx + 9, cy + 8, COLOR_BLACK);
		break;
	case 1: /* APP_SWITCH: two overlapping windows */
		draw_rect(buf, cx - 16, cy - 12, 22, 22);
		fill_rect(buf, cx - 6, cy - 2, 22, 22, COLOR_WHITE);
		draw_rect(buf, cx - 6, cy - 2, 22, 22);
		break;
	case 2: /* DESKTOP_SWITCH: 2x2 grid of screens */
		draw_rect(buf, cx - 16, cy - 16, 14, 14);
		draw_rect(buf, cx + 2, cy - 16, 14, 14);
		draw_rect(buf, cx - 16, cy + 2, 14, 14);
		draw_rect(buf, cx + 2, cy + 2, 14, 14);
		break;
	default:
		break;
	}
}
