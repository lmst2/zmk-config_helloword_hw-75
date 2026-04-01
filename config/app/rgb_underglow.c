/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/led_strip.h>

#include <zmk/rgb_underglow.h>

#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)

#error "A zmk,underglow chosen node must be declared"

#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

#ifndef CONFIG_HW75_RGB_UNDERGLOW_HUE_START
#define CONFIG_HW75_RGB_UNDERGLOW_HUE_START 0
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_SAT_START
#define CONFIG_HW75_RGB_UNDERGLOW_SAT_START 100
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_BRT_START
#define CONFIG_HW75_RGB_UNDERGLOW_BRT_START 40
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_SPD_START
#define CONFIG_HW75_RGB_UNDERGLOW_SPD_START 2
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_EFF_START
#define CONFIG_HW75_RGB_UNDERGLOW_EFF_START 0
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_HUE_STEP
#define CONFIG_HW75_RGB_UNDERGLOW_HUE_STEP 10
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_SAT_STEP
#define CONFIG_HW75_RGB_UNDERGLOW_SAT_STEP 5
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_BRT_STEP
#define CONFIG_HW75_RGB_UNDERGLOW_BRT_STEP 5
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_BRT_MIN
#define CONFIG_HW75_RGB_UNDERGLOW_BRT_MIN 0
#endif
#ifndef CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX
#define CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX 100
#endif

#define RGB_TICK_MS 30
#define RGB_KEY_FIRST_INDEX 16
#define RGB_KEY_COUNT 82
#define RGB_STATUS_FIRST_INDEX 98
#define RGB_STATUS_COUNT 3
#define RGB_REACTIVE_MAX_BRIGHTNESS 255
#define RGB_RIPPLE_MAX_COUNT 10
#define RGB_RIPPLE_LIFE_MS 1000
#define RGB_RIPPLE_RING_WIDTH 22

enum rgb_underglow_effect {
	UNDERGLOW_EFFECT_SOLID,
	UNDERGLOW_EFFECT_BREATHE,
	UNDERGLOW_EFFECT_SPECTRUM,
	UNDERGLOW_EFFECT_SWIRL,
	UNDERGLOW_EFFECT_RAINBOW_SWEEP,
	UNDERGLOW_EFFECT_REACTIVE,
	UNDERGLOW_EFFECT_AURORA,
	UNDERGLOW_EFFECT_RIPPLE,
	UNDERGLOW_EFFECT_STATIC,
	UNDERGLOW_EFFECT_NUMBER,
};

struct rgb_underglow_state {
	struct zmk_led_hsb color;
	uint8_t animation_speed;
	uint8_t current_effect;
	uint32_t animation_step;
	bool on;
};

struct rgb_ripple {
	bool active;
	uint8_t x;
	uint8_t y;
	uint32_t start_ms;
};

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static struct rgb_underglow_state state;
static uint8_t reactive_brightness[STRIP_NUM_PIXELS];
static struct rgb_ripple ripples[RGB_RIPPLE_MAX_COUNT];
static uint8_t next_ripple_slot;
static struct k_mutex lock;

static bool rgb_effect_is_animated(uint8_t effect)
{
	switch (effect) {
	case UNDERGLOW_EFFECT_SOLID:
	case UNDERGLOW_EFFECT_STATIC:
		return false;
	default:
		return true;
	}
}

static inline uint8_t qadd8(uint8_t a, uint8_t b)
{
	uint16_t sum = a + b;

	return sum > 255 ? 255 : sum;
}

static inline uint8_t qsub8(uint8_t a, uint8_t b)
{
	return a > b ? a - b : 0;
}

static uint8_t sin8(uint8_t theta)
{
	static const uint8_t lut[] = {
		0, 6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
		98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162,
		167, 171, 176, 181, 185, 189, 193, 197, 201, 205, 209, 212, 216,
		219, 222, 225, 228, 231, 234, 236, 238, 241, 243, 245, 246, 248,
		249, 251, 252, 253, 254, 254, 255, 255, 255,
	};
	uint8_t idx = theta & 0x3F;
	uint8_t val;

	if (theta & 0x40) {
		idx = 63 - idx;
	}

	val = lut[idx];
	return (theta & 0x80) ? (128 - (val + 1) / 2) : (128 + val / 2);
}

static uint8_t approx_dist(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2)
{
	uint8_t dx = x1 > x2 ? x1 - x2 : x2 - x1;
	uint8_t dy = y1 > y2 ? y1 - y2 : y2 - y1;

	return dx > dy ? dx + ((dy * 3) >> 3) : dy + ((dx * 3) >> 3);
}

static struct zmk_led_hsb hsb_scale_min_max(struct zmk_led_hsb hsb)
{
	hsb.b = CONFIG_HW75_RGB_UNDERGLOW_BRT_MIN +
		(CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX - CONFIG_HW75_RGB_UNDERGLOW_BRT_MIN) *
			hsb.b / BRT_MAX;
	return hsb;
}

static struct zmk_led_hsb hsb_scale_zero_max(struct zmk_led_hsb hsb)
{
	hsb.b = hsb.b * CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX / BRT_MAX;
	return hsb;
}

static struct led_rgb rgb_scale_zero_max(struct led_rgb rgb)
{
	return (struct led_rgb){
		.r = rgb.r * CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX / BRT_MAX,
		.g = rgb.g * CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX / BRT_MAX,
		.b = rgb.b * CONFIG_HW75_RGB_UNDERGLOW_BRT_MAX / BRT_MAX,
	};
}

static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb)
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	uint8_t i = hsb.h / 60;
	float v = hsb.b / (float)BRT_MAX;
	float s = hsb.s / (float)SAT_MAX;
	float f = hsb.h / (float)HUE_MAX * 6.0f - i;
	float p = v * (1.0f - s);
	float q = v * (1.0f - f * s);
	float t = v * (1.0f - (1.0f - f) * s);

	switch (i % 6) {
	case 0:
		r = v;
		g = t;
		b = p;
		break;
	case 1:
		r = q;
		g = v;
		b = p;
		break;
	case 2:
		r = p;
		g = v;
		b = t;
		break;
	case 3:
		r = p;
		g = q;
		b = v;
		break;
	case 4:
		r = t;
		g = p;
		b = v;
		break;
	default:
		r = v;
		g = p;
		b = q;
		break;
	}

	return (struct led_rgb){
		.r = r * 255.0f,
		.g = g * 255.0f,
		.b = b * 255.0f,
	};
}

static bool rgb_get_led_position(uint8_t idx, uint8_t *x, uint8_t *y)
{
	static const uint8_t row6x[] = {0, 20, 40, 110, 170, 186, 202, 214, 226, 240};

	if (STRIP_NUM_PIXELS >= 101) {
		if (idx < 16) {
			*x = idx * 16;
			*y = 0;
		} else if (idx < 30) {
			*x = (idx - 16) * 17;
			*y = 16;
		} else if (idx < 45) {
			*x = (idx - 30) * 16;
			*y = 32;
		} else if (idx < 60) {
			*x = 6 + (idx - 45) * 16;
			*y = 48;
		} else if (idx < 74) {
			*x = 8 + (idx - 60) * 17;
			*y = 64;
		} else if (idx < 88) {
			*x = 12 + (idx - 74) * 16;
			*y = 80;
		} else if (idx < 98) {
			*x = row6x[idx - 88];
			*y = 96;
		} else {
			*x = 232 + (idx - 98) * 4;
			*y = 80;
		}
		return true;
	}

	if (STRIP_NUM_PIXELS == 4) {
		*x = idx * 24;
		*y = 0;
		return true;
	}

	*x = (idx % 12) * 16;
	*y = (idx / 12) * 16;
	return true;
}

static bool rgb_key_position_to_led_index(uint32_t position, uint8_t *led_index)
{
	if (STRIP_NUM_PIXELS < RGB_STATUS_FIRST_INDEX || position >= RGB_KEY_COUNT) {
		return false;
	}

	*led_index = RGB_KEY_FIRST_INDEX + position;
	return true;
}

static void rgb_render_solid(void)
{
	struct led_rgb color = hsb_to_rgb(hsb_scale_min_max(state.color));

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = color;
	}
}

static void rgb_render_breathe(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;

		hsb.b = abs((int32_t)(state.animation_step % 2400) - 1200) / 12;
		pixels[i] = hsb_to_rgb(hsb_scale_zero_max(hsb));
	}

	state.animation_step += state.animation_speed * 10;
}

static void rgb_render_spectrum(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;

		hsb.h = state.animation_step % HUE_MAX;
		pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
	}

	state.animation_step = (state.animation_step + state.animation_speed) % HUE_MAX;
}

static void rgb_render_swirl(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;

		hsb.h = (HUE_MAX / MAX(1, STRIP_NUM_PIXELS) * i + state.animation_step) % HUE_MAX;
		pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
	}

	state.animation_step = (state.animation_step + state.animation_speed * 2) % HUE_MAX;
}

static void rgb_render_rainbow_sweep(void)
{
	uint32_t tick = state.animation_step;

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px;
		uint8_t py;
		uint8_t hue;
		uint8_t wave;
		uint8_t val;

		rgb_get_led_position(i, &px, &py);
		hue = px - tick / 8;
		wave = sin8(tick / 5 + px / 2);
		val = 178 + ((uint16_t)wave * 77 >> 8);

		pixels[i] = hsb_to_rgb(hsb_scale_zero_max((struct zmk_led_hsb){
			.h = hue,
			.s = SAT_MAX,
			.b = val * BRT_MAX / 255,
		}));
	}

	state.animation_step += MAX(1, state.animation_speed * 2);
}

static void rgb_render_reactive(void)
{
	uint8_t decay = 2 + state.animation_speed * 2;

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t b = reactive_brightness[i];

		reactive_brightness[i] = qsub8(reactive_brightness[i], decay);
		if (b > 0) {
			uint8_t hue = 128 + ((255 - b) / 4);
			uint8_t sat = b > 200 ? (uint8_t)(200 + (255 - b) / 2) : 255;

			pixels[i] = hsb_to_rgb(hsb_scale_zero_max((struct zmk_led_hsb){
				.h = hue,
				.s = sat * SAT_MAX / 255,
				.b = b * BRT_MAX / 255,
			}));
		} else {
			pixels[i] = (struct led_rgb){0};
		}
	}
}

static void rgb_render_aurora(void)
{
	uint32_t tick = state.animation_step;

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px;
		uint8_t py;
		uint8_t w1;
		uint8_t w2;
		uint8_t w3;
		uint8_t w4;
		int16_t hue;
		uint8_t val;
		uint8_t sat = 255;
		struct led_rgb c;

		rgb_get_led_position(i, &px, &py);
		w1 = sin8(tick / 11 + px / 2);
		w2 = sin8(tick / 7 + (240 - px) / 2);
		w3 = sin8(tick / 13 + px / 3 + py);
		w4 = sin8(tick / 17 + (240 - px) / 3);

		hue = 110 + ((int16_t)(w1 - 128) + (int16_t)(w4 - 128)) / 6;
		val = ((uint16_t)w2 + w3) >> 1;
		if (val > 200) {
			sat = qsub8(255, (val - 200) * 4);
		}

		c = hsb_to_rgb(hsb_scale_zero_max((struct zmk_led_hsb){
			.h = CLAMP(hue, 0, HUE_MAX - 1),
			.s = sat * SAT_MAX / 255,
			.b = val * BRT_MAX / 255,
		}));
		pixels[i] = rgb_scale_zero_max((struct led_rgb){
			.r = qadd8(c.r, 2),
			.g = qadd8((uint16_t)c.g * 200 >> 8, 5),
			.b = qadd8((uint16_t)c.b * 145 >> 8, 7),
		});
	}

	state.animation_step += MAX(1, state.animation_speed * 2);
}

static void rgb_render_ripple(void)
{
	uint32_t now_ms = k_uptime_get_32();
	uint32_t speed_scale = MAX(1, state.animation_speed);
	uint8_t decay = 10;

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px;
		uint8_t py;
		uint8_t best_bright = 0;
		uint8_t best_hue = 0;

		reactive_brightness[i] = qsub8(reactive_brightness[i], decay);
		rgb_get_led_position(i, &px, &py);

		for (int r = 0; r < RGB_RIPPLE_MAX_COUNT; r++) {
			uint32_t elapsed_ms;
			uint32_t elapsed_scaled;
			uint8_t radius;
			uint8_t dist;
			int16_t ring_delta;
			uint8_t ring;
			uint8_t fade;
			uint8_t bright;

			if (!ripples[r].active) {
				continue;
			}

			elapsed_ms = now_ms - ripples[r].start_ms;
			if (elapsed_ms > RGB_RIPPLE_LIFE_MS) {
				ripples[r].active = false;
				continue;
			}

			elapsed_scaled = elapsed_ms * speed_scale;
			radius = (elapsed_scaled * 65) >> 8;
			dist = approx_dist(px, py, ripples[r].x, ripples[r].y);
			ring_delta = abs((int16_t)dist - radius);
			if (ring_delta >= RGB_RIPPLE_RING_WIDTH) {
				continue;
			}

			ring = (RGB_RIPPLE_RING_WIDTH - ring_delta) * (255 / RGB_RIPPLE_RING_WIDTH);
			fade = 255 - MIN(255, elapsed_ms * 255 / RGB_RIPPLE_LIFE_MS);
			bright = ((uint16_t)ring * fade) >> 8;

			if (bright > best_bright) {
				best_bright = bright;
				best_hue = ripples[r].x + ripples[r].y * 2 + elapsed_ms / 6;
			}
		}

		if (best_bright > reactive_brightness[i]) {
			pixels[i] = hsb_to_rgb(hsb_scale_zero_max((struct zmk_led_hsb){
				.h = best_hue,
				.s = SAT_MAX,
				.b = best_bright * BRT_MAX / 255,
			}));
		} else if (reactive_brightness[i] > 0) {
			pixels[i] = hsb_to_rgb(hsb_scale_zero_max((struct zmk_led_hsb){
				.h = state.color.h,
				.s = state.color.s,
				.b = reactive_brightness[i] * BRT_MAX / 255,
			}));
		} else {
			pixels[i] = (struct led_rgb){0};
		}
	}
}

static void rgb_render_static(void)
{
	struct led_rgb color = {.r = 255, .g = 180, .b = 80};
	color = rgb_scale_zero_max(color);

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = color;
	}
}

static void zmk_rgb_underglow_tick(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	k_mutex_lock(&lock, K_FOREVER);

	switch (state.current_effect) {
	case UNDERGLOW_EFFECT_SOLID:
		rgb_render_solid();
		break;
	case UNDERGLOW_EFFECT_BREATHE:
		rgb_render_breathe();
		break;
	case UNDERGLOW_EFFECT_SPECTRUM:
		rgb_render_spectrum();
		break;
	case UNDERGLOW_EFFECT_SWIRL:
		rgb_render_swirl();
		break;
	case UNDERGLOW_EFFECT_RAINBOW_SWEEP:
		rgb_render_rainbow_sweep();
		break;
	case UNDERGLOW_EFFECT_REACTIVE:
		rgb_render_reactive();
		break;
	case UNDERGLOW_EFFECT_AURORA:
		rgb_render_aurora();
		break;
	case UNDERGLOW_EFFECT_RIPPLE:
		rgb_render_ripple();
		break;
	case UNDERGLOW_EFFECT_STATIC:
		rgb_render_static();
		break;
	default:
		rgb_render_solid();
		break;
	}

	err = led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
	k_mutex_unlock(&lock);

	if (err < 0) {
		LOG_ERR("Failed to update the RGB strip (%d)", err);
	}
}

K_WORK_DEFINE(underglow_tick_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (!state.on) {
		return;
	}

	k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

static int zmk_rgb_underglow_schedule_render(void)
{
	int ret;

	if (!led_strip) {
		return -ENODEV;
	}

	if (!state.on) {
		k_timer_stop(&underglow_tick);
		return 0;
	}

	if (rgb_effect_is_animated(state.current_effect)) {
		k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(RGB_TICK_MS));
	} else {
		k_timer_stop(&underglow_tick);
	}

	ret = k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
	return ret < 0 ? ret : 0;
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int rgb_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	int rc;

	if (settings_name_steq(name, "state", &next) && !next) {
		if (len != sizeof(state)) {
			return -EINVAL;
		}

		rc = read_cb(cb_arg, &state, sizeof(state));
		if (rc >= 0) {
			if (state.current_effect >= UNDERGLOW_EFFECT_NUMBER) {
				state.current_effect = UNDERGLOW_EFFECT_SOLID;
			}
			return 0;
		}

		return rc;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(rgb_underglow, "rgb/underglow", NULL, rgb_settings_set, NULL, NULL);

static void zmk_rgb_underglow_save_state_work(struct k_work *_work)
{
	settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_work_delayable underglow_save_work;
#endif

static int zmk_rgb_underglow_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
	if (!device_is_ready(led_strip)) {
		return -ENODEV;
	}

	k_mutex_init(&lock);

	state = (struct rgb_underglow_state){
		.color =
			{
				.h = CONFIG_HW75_RGB_UNDERGLOW_HUE_START,
				.s = CONFIG_HW75_RGB_UNDERGLOW_SAT_START,
				.b = CONFIG_HW75_RGB_UNDERGLOW_BRT_START,
			},
		.animation_speed = CONFIG_HW75_RGB_UNDERGLOW_SPD_START,
		.current_effect = MIN(CONFIG_HW75_RGB_UNDERGLOW_EFF_START,
				     UNDERGLOW_EFFECT_NUMBER - 1),
		.animation_step = 0,
		.on = IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_ON_START),
	};

#if IS_ENABLED(CONFIG_SETTINGS)
	k_work_init_delayable(&underglow_save_work, zmk_rgb_underglow_save_state_work);
#endif

#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_USB)
	state.on = zmk_usb_is_powered();
#endif

	if (state.on) {
		zmk_rgb_underglow_schedule_render();
	}

	return 0;
}

int zmk_rgb_underglow_save_state(void)
{
#if IS_ENABLED(CONFIG_SETTINGS)
	int ret = k_work_reschedule(&underglow_save_work,
				    K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));

	return MIN(ret, 0);
#else
	return 0;
#endif
}

int zmk_rgb_underglow_get_state(bool *on_off)
{
	if (!led_strip) {
		return -ENODEV;
	}

	*on_off = state.on;
	return 0;
}

int zmk_rgb_underglow_get_hsb(struct zmk_led_hsb *color)
{
	if (!led_strip) {
		return -ENODEV;
	}
	if (color == NULL) {
		return -EINVAL;
	}

	*color = state.color;
	return 0;
}

int zmk_rgb_underglow_get_speed(uint8_t *speed)
{
	if (!led_strip) {
		return -ENODEV;
	}
	if (speed == NULL) {
		return -EINVAL;
	}

	*speed = state.animation_speed;
	return 0;
}

int zmk_rgb_underglow_get_effect(uint8_t *effect)
{
	if (!led_strip) {
		return -ENODEV;
	}
	if (effect == NULL) {
		return -EINVAL;
	}

	*effect = state.current_effect;
	return 0;
}

int zmk_rgb_underglow_on(void)
{
	if (!led_strip) {
		return -ENODEV;
	}

	k_mutex_lock(&lock, K_FOREVER);
	state.on = true;
	state.animation_step = 0;
	k_mutex_unlock(&lock);

	zmk_rgb_underglow_schedule_render();
	return zmk_rgb_underglow_save_state();
}

static void zmk_rgb_underglow_off_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&lock, K_FOREVER);
	memset(pixels, 0, sizeof(pixels));
	k_mutex_unlock(&lock);

	led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

K_WORK_DEFINE(underglow_off_work, zmk_rgb_underglow_off_handler);

int zmk_rgb_underglow_off(void)
{
	if (!led_strip) {
		return -ENODEV;
	}

	k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);
	k_timer_stop(&underglow_tick);

	k_mutex_lock(&lock, K_FOREVER);
	state.on = false;
	k_mutex_unlock(&lock);

	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_calc_effect(int direction)
{
	return (state.current_effect + UNDERGLOW_EFFECT_NUMBER + direction) %
	       UNDERGLOW_EFFECT_NUMBER;
}

int zmk_rgb_underglow_select_effect(int effect)
{
	if (!led_strip) {
		return -ENODEV;
	}

	if (effect < 0 || effect >= UNDERGLOW_EFFECT_NUMBER) {
		return -EINVAL;
	}

	k_mutex_lock(&lock, K_FOREVER);
	state.current_effect = effect;
	state.animation_step = 0;
	k_mutex_unlock(&lock);

	zmk_rgb_underglow_schedule_render();
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_cycle_effect(int direction)
{
	return zmk_rgb_underglow_select_effect(zmk_rgb_underglow_calc_effect(direction));
}

int zmk_rgb_underglow_toggle(void)
{
	return state.on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on();
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color)
{
	if (color.h >= HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
		return -ENOTSUP;
	}

	k_mutex_lock(&lock, K_FOREVER);
	state.color = color;
	k_mutex_unlock(&lock);

	zmk_rgb_underglow_schedule_render();
	return zmk_rgb_underglow_save_state();
}

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction)
{
	struct zmk_led_hsb color = state.color;

	color.h += HUE_MAX + (direction * CONFIG_HW75_RGB_UNDERGLOW_HUE_STEP);
	color.h %= HUE_MAX;

	return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction)
{
	struct zmk_led_hsb color = state.color;
	int s = color.s + (direction * CONFIG_HW75_RGB_UNDERGLOW_SAT_STEP);

	color.s = CLAMP(s, 0, SAT_MAX);
	return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction)
{
	struct zmk_led_hsb color = state.color;
	int b = color.b + (direction * CONFIG_HW75_RGB_UNDERGLOW_BRT_STEP);

	color.b = CLAMP(b, 0, BRT_MAX);
	return color;
}

int zmk_rgb_underglow_change_hue(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}

	return zmk_rgb_underglow_set_hsb(zmk_rgb_underglow_calc_hue(direction));
}

int zmk_rgb_underglow_change_sat(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}

	return zmk_rgb_underglow_set_hsb(zmk_rgb_underglow_calc_sat(direction));
}

int zmk_rgb_underglow_change_brt(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}

	return zmk_rgb_underglow_set_hsb(zmk_rgb_underglow_calc_brt(direction));
}

int zmk_rgb_underglow_change_spd(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (state.animation_speed == 1 && direction < 0) {
		k_mutex_unlock(&lock);
		return 0;
	}

	state.animation_speed = CLAMP((int)state.animation_speed + direction, 1, 5);
	k_mutex_unlock(&lock);

	zmk_rgb_underglow_schedule_render();
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_set_speed(uint8_t speed)
{
	if (!led_strip) {
		return -ENODEV;
	}

	k_mutex_lock(&lock, K_FOREVER);
	state.animation_speed = CLAMP((int)speed, 1, 5);
	k_mutex_unlock(&lock);

	zmk_rgb_underglow_schedule_render();
	return zmk_rgb_underglow_save_state();
}

static void rgb_mark_key_pressed(uint32_t position)
{
	uint8_t led_index;
	uint8_t x;
	uint8_t y;

	if (!rgb_key_position_to_led_index(position, &led_index)) {
		return;
	}

	reactive_brightness[led_index] = RGB_REACTIVE_MAX_BRIGHTNESS;
	if (!rgb_get_led_position(led_index, &x, &y)) {
		return;
	}

	ripples[next_ripple_slot] = (struct rgb_ripple){
		.active = true,
		.x = x,
		.y = y,
		.start_ms = k_uptime_get_32(),
	};
	next_ripple_slot = (next_ripple_slot + 1) % RGB_RIPPLE_MAX_COUNT;
}

#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_IDLE) || IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_USB)
struct rgb_underglow_sleep_state {
	bool is_awake;
	bool rgb_state_before_sleeping;
};

static int rgb_underglow_auto_state(bool target_wake_state)
{
	static struct rgb_underglow_sleep_state sleep_state = {
		.is_awake = true,
		.rgb_state_before_sleeping = false,
	};

	if (target_wake_state == sleep_state.is_awake) {
		return 0;
	}
	sleep_state.is_awake = target_wake_state;

	if (sleep_state.is_awake) {
		return sleep_state.rgb_state_before_sleeping ? zmk_rgb_underglow_on()
							    : zmk_rgb_underglow_off();
	}

	sleep_state.rgb_state_before_sleeping = state.on;
	return zmk_rgb_underglow_off();
}
#endif

static int rgb_underglow_event_listener(const zmk_event_t *eh)
{
	struct zmk_position_state_changed *position_ev = as_zmk_position_state_changed(eh);

	if (position_ev != NULL && position_ev->state) {
		k_mutex_lock(&lock, K_FOREVER);
		rgb_mark_key_pressed(position_ev->position);
		k_mutex_unlock(&lock);

		if (state.on &&
		    (state.current_effect == UNDERGLOW_EFFECT_REACTIVE ||
		     state.current_effect == UNDERGLOW_EFFECT_RIPPLE)) {
			k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
		}
		return 0;
	}

#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_IDLE)
	if (as_zmk_activity_state_changed(eh)) {
		return rgb_underglow_auto_state(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
	}
#endif

#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_USB)
	if (as_zmk_usb_conn_state_changed(eh)) {
		return rgb_underglow_auto_state(zmk_usb_is_powered());
	}
#endif

	return -ENOTSUP;
}

ZMK_LISTENER(rgb_underglow, rgb_underglow_event_listener);
ZMK_SUBSCRIPTION(rgb_underglow, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_activity_state_changed);
#endif
#if IS_ENABLED(CONFIG_HW75_RGB_UNDERGLOW_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_usb_conn_state_changed);
#endif

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
