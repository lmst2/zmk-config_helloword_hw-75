/*
 * Copyright (c) 2020 The ZMK Contributors
 * Copyright (c) 2025 HelloWord-style effects port
 * SPDX-License-Identifier: MIT
 *
 * Replaces ZMK stock rgb_underglow when CONFIG_HW75_EXTENDED_RGB is enabled.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zephyr/drivers/led_strip.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "zmk,underglow chosen node required"
#endif

#define STRIP_CHOSEN     DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

BUILD_ASSERT(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN <= CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX,
	     "RGB underglow BRT_MIN must be <= BRT_MAX");

enum rgb_underglow_effect {
	UNDERGLOW_EFFECT_SOLID = 0,
	UNDERGLOW_EFFECT_BREATHE,
	UNDERGLOW_EFFECT_SPECTRUM,
	UNDERGLOW_EFFECT_SWIRL,
	UNDERGLOW_EFFECT_RAINBOW_SWEEP,
	UNDERGLOW_EFFECT_REACTIVE,
	UNDERGLOW_EFFECT_AURORA,
	UNDERGLOW_EFFECT_RIPPLE,
	UNDERGLOW_EFFECT_STATIC_WARM,
	UNDERGLOW_EFFECT_NUMBER,
};

struct rgb_underglow_state {
	struct zmk_led_hsb color;
	uint8_t animation_speed;
	uint8_t current_effect;
	uint16_t animation_step;
	bool on;
};

static const struct device *led_strip;
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static struct rgb_underglow_state state;
static uint8_t key_brightness[STRIP_NUM_PIXELS];
static bool key_pressed[88];

static uint8_t sin8(uint8_t theta)
{
	static const uint8_t lut[] = {
		0,   6,   13,  19,  25,  31,  37,  44,  50,  56,  62,  68,  74,  80,  86,  92,
		98,  103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
		181, 185, 189, 193, 197, 201, 205, 209, 212, 216, 219, 222, 225, 228, 231, 234,
		236, 238, 241, 243, 245, 246, 248, 249, 251, 252, 253, 254, 254, 255, 255, 255};
	uint8_t idx = theta & 0x3F;
	if (theta & 0x40) {
		idx = 63 - idx;
	}
	uint8_t val = lut[idx];
	return (theta & 0x80) ? (128 - (val + 1) / 2) : (128 + val / 2);
}

static inline uint8_t qadd8(uint8_t a, uint8_t b)
{
	uint16_t s = (uint16_t)a + b;
	return s > 255 ? 255 : (uint8_t)s;
}

static inline uint8_t qsub8(uint8_t a, uint8_t b)
{
	return a > b ? a - b : 0;
}

static struct led_rgb hsv_to_rgb_u8(uint8_t h, uint8_t s, uint8_t v)
{
	if (s == 0) {
		return (struct led_rgb){.r = v, .g = v, .b = v};
	}

	uint8_t region = h / 43;
	uint8_t remainder = (h - region * 43) * 6;
	uint8_t p = ((uint16_t)v * (255 - s)) >> 8;
	uint8_t q = ((uint16_t)v * (255 - (((uint16_t)s * remainder) >> 8))) >> 8;
	uint8_t t = ((uint16_t)v * (255 - (((uint16_t)s * (255 - remainder)) >> 8))) >> 8;

	switch (region) {
	case 0:
		return (struct led_rgb){.r = v, .g = t, .b = p};
	case 1:
		return (struct led_rgb){.r = q, .g = v, .b = p};
	case 2:
		return (struct led_rgb){.r = p, .g = v, .b = t};
	case 3:
		return (struct led_rgb){.r = p, .g = q, .b = v};
	case 4:
		return (struct led_rgb){.r = t, .g = p, .b = v};
	default:
		return (struct led_rgb){.r = v, .g = p, .b = q};
	}
}

static struct zmk_led_hsb hsb_scale_min_max(struct zmk_led_hsb hsb)
{
	hsb.b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
		(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * hsb.b / BRT_MAX;
	return hsb;
}

static struct zmk_led_hsb hsb_scale_zero_max(struct zmk_led_hsb hsb)
{
	hsb.b = hsb.b * CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX / BRT_MAX;
	return hsb;
}

static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb)
{
	float r = 0, g = 0, b = 0;
	uint8_t i = hsb.h / 60;
	float v = hsb.b / ((float)BRT_MAX);
	float s = hsb.s / ((float)SAT_MAX);
	float f = hsb.h / ((float)HUE_MAX) * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);

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
	case 5:
		r = v;
		g = p;
		b = q;
		break;
	}

	return (struct led_rgb){.r = (uint8_t)(r * 255), .g = (uint8_t)(g * 255), .b = (uint8_t)(b * 255)};
}

static void get_led_pos(uint8_t idx, uint8_t *x, uint8_t *y)
{
	if (idx < 14) {
		*y = 0;
		*x = (uint8_t)((13 - idx) * 17);
	} else if (idx < 29) {
		*y = 16;
		*x = (uint8_t)((idx - 14) * 16);
	} else if (idx < 44) {
		*y = 32;
		*x = (uint8_t)(6 + (43 - idx) * 16);
	} else if (idx < 58) {
		*y = 48;
		*x = (uint8_t)(8 + (idx - 44) * 17);
	} else if (idx < 72) {
		*y = 64;
		*x = (uint8_t)(12 + (71 - idx) * 16);
	} else if (idx < 82) {
		*y = 80;
		static const uint8_t row5x[] = {0, 20, 40, 110, 170, 186, 202, 214, 226, 240};
		*x = row5x[idx - 72];
	} else if (idx < 85) {
		*y = 80;
		*x = (uint8_t)(232 + (idx - 82) * 4);
	} else {
		*y = 96;
		*x = (uint8_t)((idx - 85) * 13);
	}
}

static uint8_t approx_dist(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2)
{
	uint8_t dx = x1 > x2 ? x1 - x2 : x2 - x1;
	uint8_t dy = y1 > y2 ? y1 - y2 : y2 - y1;
	return dx > dy ? dx + ((dy * 3) >> 3) : dy + ((dx * 3) >> 3);
}

static uint8_t key_pos_to_led(uint8_t pos)
{
	if (pos >= 82) {
		return STRIP_NUM_PIXELS - 1;
	}
	uint8_t i = pos;
	uint8_t led = i;
	if (i < 14) {
		led = 13 - i;
	} else if (i >= 29 && i < 44) {
		led = (uint8_t)(72 - i);
	} else if (i >= 58 && i < 72) {
		led = (uint8_t)(129 - i);
	}
	if (led >= STRIP_NUM_PIXELS) {
		return STRIP_NUM_PIXELS - 1;
	}
	return led;
}

static void zmk_rgb_underglow_effect_solid(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = hsb_to_rgb(hsb_scale_min_max(state.color));
	}
}

static void zmk_rgb_underglow_effect_breathe(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;
		hsb.b = abs((int)state.animation_step - 1200) / 12;
		pixels[i] = hsb_to_rgb(hsb_scale_zero_max(hsb));
	}
	state.animation_step += state.animation_speed * 10;
	if (state.animation_step > 2400) {
		state.animation_step = 0;
	}
}

static void zmk_rgb_underglow_effect_spectrum(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;
		hsb.h = state.animation_step;
		pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
	}
	state.animation_step += state.animation_speed;
	state.animation_step = state.animation_step % HUE_MAX;
}

static void zmk_rgb_underglow_effect_swirl(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		struct zmk_led_hsb hsb = state.color;
		hsb.h = (HUE_MAX / STRIP_NUM_PIXELS * i + state.animation_step) % HUE_MAX;
		pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
	}
	state.animation_step += state.animation_speed * 2;
	state.animation_step = state.animation_step % HUE_MAX;
}

static void effect_rainbow_sweep(uint32_t tick)
{
	for (uint8_t i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px, py;
		get_led_pos(i, &px, &py);
		uint8_t hue = (uint8_t)(px - tick / 8);
		uint8_t wave = sin8((uint8_t)(tick / 5 + px / 2));
		uint8_t val = (uint8_t)(178 + (((uint16_t)wave * 77) >> 8));
		pixels[i] = hsv_to_rgb_u8(hue, 255, val);
	}
}

static void effect_reactive(uint32_t tick)
{
	static uint32_t last_decay;

	if (tick - last_decay >= 3) {
		last_decay = tick;
		for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
			key_brightness[i] = qsub8(key_brightness[i], 1);
		}
	}

	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t b = key_brightness[i];
		if (b > 0) {
			uint8_t hue = (uint8_t)(128 + (255 - b) / 4);
			uint8_t sat = b > 200 ? (uint8_t)(200 + (255 - b) / 2) : 255;
			pixels[i] = hsv_to_rgb_u8(hue, sat, b);
		} else {
			pixels[i] = (struct led_rgb){0};
		}
	}
}

static void effect_aurora(uint32_t tick)
{
	for (uint8_t i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px, py;
		get_led_pos(i, &px, &py);
		uint8_t w1 = sin8((uint8_t)(tick / 11 + px / 2));
		uint8_t w2 = sin8((uint8_t)(tick / 7 + (240 - px) / 2));
		uint8_t w3 = sin8((uint8_t)(tick / 13 + px / 3 + py));
		uint8_t w4 = sin8((uint8_t)(tick / 17 + (240 - px) / 3));
		uint8_t hue = (uint8_t)(110 + (w1 - 128 + w4 - 128) / 6);
		uint8_t val = (uint8_t)(((uint16_t)w2 + w3) >> 1);
		uint8_t sat = 255;
		if (val > 200) {
			sat = qsub8(255, (val - 200) * 4);
		}
		struct led_rgb c = hsv_to_rgb_u8(hue, sat, val);
		uint8_t r = qadd8(c.r, 2);
		uint8_t g = qadd8((uint8_t)(((uint16_t)c.g * 200) >> 8), 5);
		uint8_t b = qadd8((uint8_t)(((uint16_t)c.b * 145) >> 8), 7);
		pixels[i] = (struct led_rgb){.r = r, .g = g, .b = b};
	}
}

static void effect_ripple(uint32_t tick)
{
	static const uint8_t MAX_RIPPLES = 10;
	static const uint16_t RIPPLE_LIFE = 1000;
	static const uint8_t RING_WIDTH = 22;
	static struct {
		uint8_t x, y;
		uint16_t start_tick;
	} ripples[10];
	static uint8_t next_slot;
	static uint8_t prev_pressed[11];

	for (uint8_t k = 0; k < 82; k++) {
		bool now = key_pressed[k];
		bool was = prev_pressed[k >> 3] & (1 << (k & 7));
		if (now && !was) {
			uint8_t px, py;
			get_led_pos(k, &px, &py);
			ripples[next_slot % MAX_RIPPLES] =
				(struct){.x = px, .y = py, .start_tick = (uint16_t)tick};
			next_slot++;
		}
		if (now) {
			prev_pressed[k >> 3] |= (uint8_t)(1 << (k & 7));
		} else {
			prev_pressed[k >> 3] &= (uint8_t) ~(1 << (k & 7));
		}
	}

	for (uint8_t i = 0; i < STRIP_NUM_PIXELS; i++) {
		uint8_t px, py;
		get_led_pos(i, &px, &py);
		uint8_t best_bright = 0;
		uint8_t best_hue = 0;
		uint8_t count = next_slot < MAX_RIPPLES ? next_slot : MAX_RIPPLES;
		for (uint8_t r = 0; r < count; r++) {
			uint16_t elapsed = (uint16_t)tick - ripples[r].start_tick;
			if (elapsed > RIPPLE_LIFE) {
				continue;
			}
			uint8_t radius = (uint8_t)((elapsed * 65) >> 8);
			uint8_t dist = approx_dist(px, py, ripples[r].x, ripples[r].y);
			int16_t ring_delta = (int16_t)dist - radius;
			if (ring_delta < 0) {
				ring_delta = -ring_delta;
			}
			if (ring_delta >= RING_WIDTH) {
				continue;
			}
			uint8_t ring = (uint8_t)((RING_WIDTH - ring_delta) * (255 / RING_WIDTH));
			uint8_t fade = (uint8_t)(255 - (((uint32_t)elapsed * 65) >> 8));
			uint8_t bright = (uint8_t)(((uint16_t)ring * fade) >> 8);
			if (bright > best_bright) {
				best_bright = bright;
				best_hue = (uint8_t)(ripples[r].x + ripples[r].y * 2 + elapsed / 6);
			}
		}
		if (best_bright > 0) {
			pixels[i] = hsv_to_rgb_u8(best_hue, 255, best_bright);
		} else {
			pixels[i] = (struct led_rgb){0};
		}
	}
}

static void effect_static_warm(void)
{
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = (struct led_rgb){.r = 255, .g = 180, .b = 80};
	}
}

static void zmk_rgb_underglow_tick(struct k_work *work)
{
	ARG_UNUSED(work);
	uint32_t tick = k_uptime_get_32();

	if (!state.on) {
		return;
	}

	switch (state.current_effect) {
	case UNDERGLOW_EFFECT_SOLID:
		zmk_rgb_underglow_effect_solid();
		break;
	case UNDERGLOW_EFFECT_BREATHE:
		zmk_rgb_underglow_effect_breathe();
		break;
	case UNDERGLOW_EFFECT_SPECTRUM:
		zmk_rgb_underglow_effect_spectrum();
		break;
	case UNDERGLOW_EFFECT_SWIRL:
		zmk_rgb_underglow_effect_swirl();
		break;
	case UNDERGLOW_EFFECT_RAINBOW_SWEEP:
		effect_rainbow_sweep(tick);
		break;
	case UNDERGLOW_EFFECT_REACTIVE:
		effect_reactive(tick);
		break;
	case UNDERGLOW_EFFECT_AURORA:
		effect_aurora(tick);
		break;
	case UNDERGLOW_EFFECT_RIPPLE:
		effect_ripple(tick);
		break;
	case UNDERGLOW_EFFECT_STATIC_WARM:
		effect_static_warm();
		break;
	default:
		zmk_rgb_underglow_effect_solid();
		break;
	}

	unsigned int key = irq_lock();
	int err = led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
	if (err < 0) {
		LOG_ERR("led_strip_update_rgb failed (%d)", err);
	}
	irq_unlock(key);
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
		return rc >= 0 ? 0 : rc;
	}
	return -ENOENT;
}

static struct settings_handler rgb_conf = {.name = "rgb/underglow", .h_set = rgb_settings_set};

static void zmk_rgb_underglow_save_state_work(struct k_work *work)
{
	ARG_UNUSED(work);
	settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_work_delayable underglow_save_work;
#endif

static int zmk_rgb_underglow_init(const struct device *_arg)
{
	ARG_UNUSED(_arg);
	led_strip = DEVICE_DT_GET(STRIP_CHOSEN);

	state = (struct rgb_underglow_state){
		.color =
			{
				.h = CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
				.s = CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
				.b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
			},
		.animation_speed = CONFIG_ZMK_RGB_UNDERGLOW_SPD_START,
		.current_effect = CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
		.animation_step = 0,
		.on = IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START),
	};

#if IS_ENABLED(CONFIG_SETTINGS)
	int err = settings_register(&rgb_conf);
	if (err) {
		LOG_ERR("settings_register rgb failed (%d)", err);
		return err;
	}
	k_work_init_delayable(&underglow_save_work, zmk_rgb_underglow_save_state_work);
	settings_load_subtree("rgb/underglow");
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
	state.on = zmk_usb_is_powered();
#endif

	if (state.on) {
		k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
	}
	return 0;
}

static int zmk_rgb_underglow_save_state(void)
{
#if IS_ENABLED(CONFIG_SETTINGS)
	int ret = k_work_reschedule(&underglow_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
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

static int zmk_rgb_underglow_on_immediate(void)
{
	if (!led_strip) {
		return -ENODEV;
	}
	state.on = true;
	state.animation_step = 0;
	k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
	return 0;
}

int zmk_rgb_underglow_on(void)
{
	zmk_rgb_underglow_on_immediate();
	return zmk_rgb_underglow_save_state();
}

static void zmk_rgb_underglow_off_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
		pixels[i] = (struct led_rgb){0};
	}
	unsigned int key = irq_lock();
	led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
	irq_unlock(key);
}

K_WORK_DEFINE(underglow_off_work, zmk_rgb_underglow_off_handler);

static int zmk_rgb_underglow_off_immediate(void)
{
	if (!led_strip) {
		return -ENODEV;
	}
	k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);
	k_timer_stop(&underglow_tick);
	state.on = false;
	return 0;
}

int zmk_rgb_underglow_off(void)
{
	zmk_rgb_underglow_off_immediate();
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_calc_effect(int direction)
{
	return (state.current_effect + UNDERGLOW_EFFECT_NUMBER + direction) % UNDERGLOW_EFFECT_NUMBER;
}

int zmk_rgb_underglow_select_effect(int effect)
{
	if (!led_strip) {
		return -ENODEV;
	}
	if (effect < 0 || effect >= UNDERGLOW_EFFECT_NUMBER) {
		return -EINVAL;
	}
	state.current_effect = (uint8_t)effect;
	state.animation_step = 0;
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
	if (color.h > HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
		return -ENOTSUP;
	}
	state.color = color;
	return 0;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction)
{
	struct zmk_led_hsb color = state.color;
	color.h += HUE_MAX + (direction * CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP);
	color.h %= HUE_MAX;
	return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction)
{
	struct zmk_led_hsb color = state.color;
	int s = color.s + (direction * CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP);
	if (s < 0) {
		s = 0;
	} else if (s > SAT_MAX) {
		s = SAT_MAX;
	}
	color.s = (uint8_t)s;
	return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction)
{
	struct zmk_led_hsb color = state.color;
	int b = color.b + (direction * CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP);
	color.b = CLAMP(b, 0, BRT_MAX);
	return color;
}

int zmk_rgb_underglow_change_hue(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}
	state.color = zmk_rgb_underglow_calc_hue(direction);
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_sat(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}
	state.color = zmk_rgb_underglow_calc_sat(direction);
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_brt(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}
	state.color = zmk_rgb_underglow_calc_brt(direction);
	return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_spd(int direction)
{
	if (!led_strip) {
		return -ENODEV;
	}
	if (state.animation_speed == 1 && direction < 0) {
		return 0;
	}
	state.animation_speed += direction;
	if (state.animation_speed > 5) {
		state.animation_speed = 5;
	}
	return zmk_rgb_underglow_save_state();
}

static int rgb_position_listener(const zmk_event_t *eh)
{
	const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
	if (!ev) {
		return -ENOTSUP;
	}
	if (ev->position < 88) {
		key_pressed[ev->position] = ev->state;
	}
	if (ev->state && ev->position < 82) {
		uint8_t led = key_pos_to_led((uint8_t)ev->position);
		key_brightness[led] = 255;
	}
	return 0;
}

ZMK_LISTENER(hello_rgb_pos, rgb_position_listener);
ZMK_SUBSCRIPTION(hello_rgb_pos, zmk_position_state_changed);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) || IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
static int rgb_underglow_auto_state(bool *prev_state, bool new_state)
{
	if (state.on == new_state) {
		return 0;
	}
	if (new_state) {
		state.on = *prev_state;
		*prev_state = false;
		return zmk_rgb_underglow_on_immediate();
	}
	state.on = false;
	*prev_state = true;
	return zmk_rgb_underglow_off_immediate();
}

static int rgb_underglow_event_listener(const zmk_event_t *eh)
{
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
	if (as_zmk_activity_state_changed(eh)) {
		static bool prev_state;
		return rgb_underglow_auto_state(&prev_state,
						zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
	}
#endif
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
	if (as_zmk_usb_conn_state_changed(eh)) {
		static bool prev_state;
		return rgb_underglow_auto_state(&prev_state, zmk_usb_is_powered());
	}
#endif
	return -ENOTSUP;
}

ZMK_LISTENER(hello_rgb_auto, rgb_underglow_event_listener);
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(hello_rgb_auto, zmk_activity_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(hello_rgb_auto, zmk_usb_conn_state_changed);
#endif

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
