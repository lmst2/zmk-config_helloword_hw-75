/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/led_strip.h>

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

struct zmk_rgb_underglow_custom_effect_context {
    struct led_rgb *pixels;
    size_t pixel_count;
    struct zmk_led_hsb color;
    uint8_t effect;
    uint8_t animation_speed;
    uint16_t animation_step;
};

int zmk_rgb_underglow_toggle();
int zmk_rgb_underglow_get_state(bool *state);
int zmk_rgb_underglow_get_speed(void);
int zmk_rgb_underglow_on();
int zmk_rgb_underglow_off();
int zmk_rgb_underglow_cycle_effect(int direction);
int zmk_rgb_underglow_calc_effect(int direction);
int zmk_rgb_underglow_select_effect(int effect);
int zmk_rgb_underglow_effect_count(void);
uint32_t zmk_rgb_underglow_effects_mask(void);
struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction);
int zmk_rgb_underglow_change_hue(int direction);
int zmk_rgb_underglow_change_sat(int direction);
int zmk_rgb_underglow_change_brt(int direction);
int zmk_rgb_underglow_change_spd(int direction);
int zmk_rgb_underglow_set_speed(uint8_t speed);
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);

int zmk_rgb_underglow_custom_effect_count(void);
uint32_t zmk_rgb_underglow_custom_effects_mask(void);
int zmk_rgb_underglow_custom_effect_render(
    const struct zmk_rgb_underglow_custom_effect_context *context);
