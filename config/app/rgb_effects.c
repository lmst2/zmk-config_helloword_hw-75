/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>

#include <app/diag_log.h>
#include <app/hw75_rgb_effects.h>

#if defined(CONFIG_BOARD_HW75_KEYBOARD)

#define HW75_KEYBOARD_LED_COUNT 103
#define HW75_KEYBOARD_HUB_LED_COUNT 18
#define HW75_KEYBOARD_KEY_LED_COUNT 82
#define HW75_KEYBOARD_KEY_LED_START HW75_KEYBOARD_HUB_LED_COUNT
#define HW75_KEYBOARD_STATUS_LED_COUNT 3
#define HW75_KEYBOARD_STATUS_LED_START                                                     \
    (HW75_KEYBOARD_KEY_LED_START + HW75_KEYBOARD_KEY_LED_COUNT)
#define HW75_RIPPLE_SLOT_COUNT 10
#define HW75_RIPPLE_LIFE_MS 1000U
#define HW75_RIPPLE_RING_WIDTH 22U

struct hw75_ripple_slot {
    uint8_t x;
    uint8_t y;
    uint32_t started_at_ms;
    bool active;
};

static uint32_t key_press_started_at_ms[HW75_KEYBOARD_LED_COUNT];
static struct hw75_ripple_slot ripple_slots[HW75_RIPPLE_SLOT_COUNT];
static uint8_t next_ripple_slot;
static uint8_t last_custom_effect = UINT8_MAX;
static uint32_t effect_started_at_ms;

static void hw75_rgb_log_stack_watermark(uint16_t effect, uint16_t stage, uint16_t pixel_count) {
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    static const uint8_t thresholds[] = {50U, 70U, 90U, 100U};
    static uint8_t threshold_index;
    size_t unused = 0U;
    size_t used = 0U;
    size_t used_pct = 0U;
    ARG_UNUSED(pixel_count);

    if (k_thread_stack_space_get(k_current_get(), &unused) != 0) {
        return;
    }

    unused = MIN(unused, (size_t)CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);
    used = (size_t)CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE - unused;
    used_pct = (used * 100U) / CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE;

    while (threshold_index < (sizeof(thresholds) / sizeof(thresholds[0])) &&
           used_pct >= thresholds[threshold_index]) {
        uint16_t stage_and_threshold =
            (uint16_t)stage | ((uint16_t)thresholds[threshold_index] << 8);

        hw75_diag_log_event(HW75_DIAG_LEVEL_WARN, HW75_DIAG_MODULE_RGB, 0U,
                            HW75_DIAG_EVENT_RGB_WORKQ_STACK_WATERMARK,
                            hw75_diag_pack_u16x2(effect, (uint16_t)unused), true,
                            hw75_diag_pack_u16x2(
                                CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE, stage_and_threshold));
        threshold_index++;
    }
#else
    ARG_UNUSED(effect);
    ARG_UNUSED(stage);
    ARG_UNUSED(pixel_count);
#endif
}

static uint8_t hw75_wave8(uint8_t theta) {
    static const uint8_t lut[] = {0,   6,   13,  19,  25,  31,  37,  44,  50,  56,  62,
                                  68,  74,  80,  86,  92,  98,  103, 109, 115, 120, 126,
                                  131, 136, 142, 147, 152, 157, 162, 167, 171, 176, 181,
                                  185, 189, 193, 197, 201, 205, 209, 212, 216, 219, 222,
                                  225, 228, 231, 234, 236, 238, 241, 243, 245, 246, 248,
                                  249, 251, 252, 253, 254, 254, 255, 255, 255};
    uint8_t idx = theta & 0x3F;
    if ((theta & 0x40U) != 0U) {
        idx = 63U - idx;
    }

    uint8_t value = lut[idx];
    return (theta & 0x80U) != 0U ? (uint8_t)(128U - (value + 1U) / 2U)
                                 : (uint8_t)(128U + value / 2U);
}

static struct led_rgb hw75_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v) {
    if (s == 0U) {
        return (struct led_rgb){.r = v, .g = v, .b = v};
    }

    uint8_t region = h / 43U;
    uint8_t remainder = (uint8_t)((h - region * 43U) * 6U);

    uint8_t p = (uint8_t)(((uint16_t)v * (255U - s)) >> 8);
    uint8_t q = (uint8_t)(((uint16_t)v * (255U - (((uint16_t)s * remainder) >> 8))) >> 8);
    uint8_t t =
        (uint8_t)(((uint16_t)v * (255U - (((uint16_t)s * (255U - remainder)) >> 8))) >> 8);

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

static struct led_rgb hw75_apply_master_brightness(struct led_rgb color, uint8_t brightness) {
    return (struct led_rgb){
        .r = (uint8_t)(((uint16_t)color.r * brightness) / 255U),
        .g = (uint8_t)(((uint16_t)color.g * brightness) / 255U),
        .b = (uint8_t)(((uint16_t)color.b * brightness) / 255U),
    };
}

static uint8_t hw75_master_brightness(const struct zmk_led_hsb *color) {
    return (uint8_t)(((uint16_t)color->b * 255U) / 100U);
}

static uint32_t hw75_effect_elapsed_ms(const struct zmk_rgb_underglow_custom_effect_context *context,
                                       uint32_t now_ms) {
    uint32_t elapsed = now_ms - effect_started_at_ms;
    return elapsed * MAX(context->animation_speed, 1U);
}

static bool hw75_keyboard_led_pos(uint8_t led, uint8_t *x, uint8_t *y) {
    static const uint8_t row5x[] = {0, 20, 40, 110, 170, 186, 202, 214, 226, 240};
    static const uint8_t hub_row_x[] = {0,  14, 28, 42, 56, 70, 84,  98, 112,
                                        126, 140, 154, 168, 182, 196, 210, 224, 238};

    if (led < HW75_KEYBOARD_HUB_LED_COUNT) {
        *y = 96U;
        *x = hub_row_x[led];
        return true;
    }

    led -= HW75_KEYBOARD_KEY_LED_START;

    if (led < 14U) {
        *y = 0U;
        *x = (uint8_t)(led * 17U);
    } else if (led < 29U) {
        *y = 16U;
        *x = (uint8_t)((led - 14U) * 16U);
    } else if (led < 44U) {
        *y = 32U;
        *x = (uint8_t)(6U + (led - 29U) * 16U);
    } else if (led < 58U) {
        *y = 48U;
        *x = (uint8_t)(8U + (led - 44U) * 17U);
    } else if (led < 72U) {
        *y = 64U;
        *x = (uint8_t)(12U + (led - 58U) * 16U);
    } else if (led < 82U) {
        *y = 80U;
        *x = row5x[led - 72U];
    } else {
        *x = 0U;
        *y = 0U;
        return false;
    }

    return true;
}

static uint8_t hw75_position_to_led(uint32_t position) {
    return (uint8_t)(HW75_KEYBOARD_KEY_LED_START + position);
}

static uint8_t hw75_approx_dist(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    uint8_t dx = x1 > x2 ? (uint8_t)(x1 - x2) : (uint8_t)(x2 - x1);
    uint8_t dy = y1 > y2 ? (uint8_t)(y1 - y2) : (uint8_t)(y2 - y1);
    return dx > dy ? (uint8_t)(dx + ((dy * 3U) >> 3)) : (uint8_t)(dy + ((dx * 3U) >> 3));
}

static void hw75_reset_runtime_state(uint32_t now_ms, uint8_t effect) {
    memset(key_press_started_at_ms, 0, sizeof(key_press_started_at_ms));
    memset(ripple_slots, 0, sizeof(ripple_slots));
    next_ripple_slot = 0U;
    last_custom_effect = effect;
    effect_started_at_ms = now_ms;
}

static void hw75_prepare_effect(const struct zmk_rgb_underglow_custom_effect_context *context,
                                uint32_t now_ms) {
    if (context->effect != last_custom_effect) {
        hw75_reset_runtime_state(now_ms, context->effect);
    }
}

static void hw75_render_rainbow_sweep(
    const struct zmk_rgb_underglow_custom_effect_context *context, uint32_t now_ms) {
    uint32_t elapsed = hw75_effect_elapsed_ms(context, now_ms);
    uint8_t master = hw75_master_brightness(&context->color);
    uint8_t hue_offset = (uint8_t)(context->color.h * 255U / 360U);

    for (uint8_t led = 0; led < context->pixel_count; led++) {
        uint8_t x;
        uint8_t y;
        if (!hw75_keyboard_led_pos(led, &x, &y)) {
            context->pixels[led] = (struct led_rgb){0};
            continue;
        }

        uint8_t hue = (uint8_t)(hue_offset + x + (elapsed / 12U));
        uint8_t wave = hw75_wave8((uint8_t)(elapsed / 20U + x * 2U + y));
        uint8_t value = (uint8_t)(150U + ((uint16_t)wave * 105U) / 255U);

        context->pixels[led] = hw75_apply_master_brightness(hw75_hsv_to_rgb(hue, 255U, value), master);
    }
}

static void hw75_render_reactive(const struct zmk_rgb_underglow_custom_effect_context *context,
                                 uint32_t now_ms) {
    uint8_t hue_offset = (uint8_t)(context->color.h * 255U / 360U);
    uint8_t master = hw75_master_brightness(&context->color);

    for (uint8_t led = 0; led < context->pixel_count; led++) {
        context->pixels[led] = (struct led_rgb){0};
    }

    for (uint8_t key_led = 0; key_led < HW75_KEYBOARD_KEY_LED_COUNT; key_led++) {
        uint8_t led = (uint8_t)(HW75_KEYBOARD_KEY_LED_START + key_led);
        uint32_t pressed_at = key_press_started_at_ms[led];
        if (pressed_at == 0U || now_ms <= pressed_at) {
            continue;
        }

        uint32_t elapsed = now_ms - pressed_at;
        if (elapsed >= 800U) {
            key_press_started_at_ms[led] = 0U;
            continue;
        }

        uint8_t brightness = (uint8_t)(255U - (elapsed * 255U) / 800U);
        uint8_t hue = (uint8_t)(hue_offset + 96U + ((255U - brightness) / 4U));
        uint8_t saturation = brightness > 200U ? (uint8_t)(200U + ((255U - brightness) / 2U)) : 255U;
        context->pixels[led] =
            hw75_apply_master_brightness(hw75_hsv_to_rgb(hue, saturation, brightness), master);
    }
}

static void hw75_render_aurora(const struct zmk_rgb_underglow_custom_effect_context *context,
                               uint32_t now_ms) {
    uint32_t elapsed = hw75_effect_elapsed_ms(context, now_ms);
    uint8_t master = hw75_master_brightness(&context->color);
    uint8_t hue_offset = (uint8_t)(context->color.h * 255U / 360U);

    for (uint8_t led = 0; led < context->pixel_count; led++) {
        uint8_t x;
        uint8_t y;
        if (!hw75_keyboard_led_pos(led, &x, &y)) {
            context->pixels[led] = (struct led_rgb){0};
            continue;
        }

        uint8_t w1 = hw75_wave8((uint8_t)(elapsed / 18U + x / 2U));
        uint8_t w2 = hw75_wave8((uint8_t)(elapsed / 11U + (240U - x) / 2U));
        uint8_t w3 = hw75_wave8((uint8_t)(elapsed / 22U + x / 3U + y));
        uint8_t w4 = hw75_wave8((uint8_t)(elapsed / 29U + (240U - x) / 3U));

        int16_t hue_shift = ((int16_t)w1 - 128) / 6 + ((int16_t)w4 - 128) / 6;
        uint8_t hue = (uint8_t)(hue_offset + 78U + hue_shift);
        uint8_t value = (uint8_t)(((uint16_t)w2 + w3) >> 1);
        uint8_t saturation = value > 200U ? (uint8_t)(255U - (value - 200U) * 4U) : 255U;

        struct led_rgb color = hw75_hsv_to_rgb(hue, saturation, value);
        color.r = MIN(255U, (uint16_t)color.r + 2U);
        color.g = MIN(255U, ((uint16_t)color.g * 200U) / 255U + 5U);
        color.b = MIN(255U, ((uint16_t)color.b * 145U) / 255U + 7U);
        context->pixels[led] = hw75_apply_master_brightness(color, master);
    }
}

static void hw75_render_ripple(const struct zmk_rgb_underglow_custom_effect_context *context,
                               uint32_t now_ms) {
    uint8_t master = hw75_master_brightness(&context->color);
    uint8_t hue_offset = (uint8_t)(context->color.h * 255U / 360U);

    for (uint8_t led = 0; led < context->pixel_count; led++) {
        uint8_t x;
        uint8_t y;
        uint8_t best_brightness = 0U;
        uint8_t best_hue = 0U;

        if (!hw75_keyboard_led_pos(led, &x, &y)) {
            context->pixels[led] = (struct led_rgb){0};
            continue;
        }

        for (uint8_t slot = 0; slot < HW75_RIPPLE_SLOT_COUNT; slot++) {
            if (!ripple_slots[slot].active || now_ms <= ripple_slots[slot].started_at_ms) {
                continue;
            }

            uint32_t elapsed = now_ms - ripple_slots[slot].started_at_ms;
            if (elapsed > HW75_RIPPLE_LIFE_MS) {
                ripple_slots[slot].active = false;
                continue;
            }

            uint8_t radius = (uint8_t)((elapsed * 65U) >> 8);
            uint8_t distance = hw75_approx_dist(x, y, ripple_slots[slot].x, ripple_slots[slot].y);
            int16_t ring_delta = (int16_t)distance - radius;
            if (ring_delta < 0) {
                ring_delta = (int16_t)-ring_delta;
            }
            if (ring_delta >= HW75_RIPPLE_RING_WIDTH) {
                continue;
            }

            uint8_t ring =
                (uint8_t)(((HW75_RIPPLE_RING_WIDTH - ring_delta) * 255U) / HW75_RIPPLE_RING_WIDTH);
            uint8_t fade = (uint8_t)(255U - ((elapsed * 65U) >> 8));
            uint8_t brightness = (uint8_t)(((uint16_t)ring * fade) >> 8);

            if (brightness > best_brightness) {
                best_brightness = brightness;
                best_hue =
                    (uint8_t)(hue_offset + ripple_slots[slot].x + ripple_slots[slot].y * 2U +
                              (elapsed / 6U));
            }
        }

        context->pixels[led] = best_brightness == 0U
                                   ? (struct led_rgb){0}
                                   : hw75_apply_master_brightness(
                                         hw75_hsv_to_rgb(best_hue, 255U, best_brightness), master);
    }
}

static void hw75_render_static(const struct zmk_rgb_underglow_custom_effect_context *context) {
    uint8_t master = hw75_master_brightness(&context->color);
    struct led_rgb warm_white = hw75_apply_master_brightness(
        (struct led_rgb){.r = 255U, .g = 180U, .b = 80U}, master);

    for (uint8_t led = 0; led < context->pixel_count; led++) {
        context->pixels[led] =
            led < HW75_KEYBOARD_STATUS_LED_START ? warm_white : (struct led_rgb){0};
    }
}

static void hw75_add_ripple_origin(uint8_t led, uint32_t now_ms) {
    struct hw75_ripple_slot *slot = &ripple_slots[next_ripple_slot % HW75_RIPPLE_SLOT_COUNT];
    hw75_keyboard_led_pos(led, &slot->x, &slot->y);
    slot->started_at_ms = now_ms;
    slot->active = true;
    next_ripple_slot++;
}

static int hw75_rgb_effects_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || !event->state || event->position >= HW75_KEYBOARD_KEY_LED_COUNT) {
        return -ENOTSUP;
    }

    uint32_t now_ms = event->timestamp > 0 ? (uint32_t)event->timestamp : k_uptime_get_32();
    uint8_t led = hw75_position_to_led(event->position);
    key_press_started_at_ms[led] = now_ms;
    hw75_add_ripple_origin(led, now_ms);

    return 0;
}

ZMK_LISTENER(hw75_rgb_effects, hw75_rgb_effects_listener);
ZMK_SUBSCRIPTION(hw75_rgb_effects, zmk_position_state_changed);

#endif

int zmk_rgb_underglow_custom_effect_count(void) {
#if defined(CONFIG_BOARD_HW75_KEYBOARD)
    return 5;
#else
    return 0;
#endif
}

uint32_t zmk_rgb_underglow_custom_effects_mask(void) {
#if defined(CONFIG_BOARD_HW75_KEYBOARD)
    return BIT(HW75_RGB_EFFECT_RAINBOW_SWEEP) | BIT(HW75_RGB_EFFECT_REACTIVE) |
           BIT(HW75_RGB_EFFECT_AURORA) | BIT(HW75_RGB_EFFECT_RIPPLE) |
           BIT(HW75_RGB_EFFECT_STATIC);
#else
    return 0;
#endif
}

int zmk_rgb_underglow_custom_effect_render(
    const struct zmk_rgb_underglow_custom_effect_context *context) {
#if !defined(CONFIG_BOARD_HW75_KEYBOARD)
    ARG_UNUSED(context);
    return -ENOTSUP;
#else
    uint32_t now_ms = k_uptime_get_32();
    hw75_rgb_log_stack_watermark((uint16_t)context->effect, 1U, (uint16_t)context->pixel_count);
    hw75_prepare_effect(context, now_ms);

    switch (context->effect) {
    case HW75_RGB_EFFECT_RAINBOW_SWEEP:
        hw75_render_rainbow_sweep(context, now_ms);
        hw75_rgb_log_stack_watermark((uint16_t)context->effect, 2U,
                                     (uint16_t)context->pixel_count);
        return 0;
    case HW75_RGB_EFFECT_REACTIVE:
        hw75_render_reactive(context, now_ms);
        hw75_rgb_log_stack_watermark((uint16_t)context->effect, 2U,
                                     (uint16_t)context->pixel_count);
        return 0;
    case HW75_RGB_EFFECT_AURORA:
        hw75_render_aurora(context, now_ms);
        hw75_rgb_log_stack_watermark((uint16_t)context->effect, 2U,
                                     (uint16_t)context->pixel_count);
        return 0;
    case HW75_RGB_EFFECT_RIPPLE:
        hw75_render_ripple(context, now_ms);
        hw75_rgb_log_stack_watermark((uint16_t)context->effect, 2U,
                                     (uint16_t)context->pixel_count);
        return 0;
    case HW75_RGB_EFFECT_STATIC:
        hw75_render_static(context);
        hw75_rgb_log_stack_watermark((uint16_t)context->effect, 2U,
                                     (uint16_t)context->pixel_count);
        return 0;
    default:
        return -EINVAL;
    }
#endif
}
