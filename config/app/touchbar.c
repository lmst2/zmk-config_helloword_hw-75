/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/keys.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include <app/hid_mouse.h>
#include <app/diag_log.h>
#include <app/indicator.h>
#include <app/kscan_74hc165.h>
#include <app/touchbar.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if defined(CONFIG_BOARD_HW75_KEYBOARD)

#define TOUCHBAR_INIT_PRIORITY 91

#define TOUCHBAR_SEGMENT_CAPACITY HW75_TOUCHBAR_MAX_SEGMENT_COUNT
#define TOUCHBAR_TOUCH_MAP_CAPACITY HW75_TOUCHBAR_CHANNEL_COUNT
#define TOUCHBAR_ENTRY_MAP_CAPACITY HW75_TOUCHBAR_CHANNEL_COUNT
#define TOUCHBAR_INVALID_SEGMENT 0xFF
#define TOUCHBAR_POLL_INTERVAL_MS 10
#define TOUCHBAR_ACTIVATION_MS 20U
#define TOUCHBAR_APP_ACTIVATION_MS 90U
#define TOUCHBAR_DESKTOP_HOLD_MS 500U
#define TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS 400U
#define TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS 1200U
#define TOUCHBAR_APP_RELEASE_SETTLE_MS 50U
#define TOUCHBAR_RELEASE_GRACE_MS 35U
#define TOUCHBAR_SWITCH_RELEASE_GRACE_MS 90U
#define TOUCHBAR_PAN_INTERVAL_MS 12U
#define TOUCHBAR_APP_STEP_INTERVAL_MS 55U
#define TOUCHBAR_DESKTOP_STEP_INTERVAL_MS 500U
#define TOUCHBAR_POSITION_SCALE 256
#define TOUCHBAR_DESKTOP_SWIPE_DISTANCE 96
#define TOUCHBAR_EDGE_REPEAT_THRESHOLD 64
#define TOUCHBAR_PAN_DEADZONE 64
#define TOUCHBAR_STEP_DISTANCE 160
#define TOUCHBAR_DESKTOP_STEP_DISTANCE 256
#define TOUCHBAR_PULSE_TICKS 2
#define TOUCHBAR_MODE_PAN HW75_TOUCHBAR_MODE_PAN
#define TOUCHBAR_MODE_APP_SWITCH HW75_TOUCHBAR_MODE_APP_SWITCH
#define TOUCHBAR_MODE_DESKTOP_SWITCH HW75_TOUCHBAR_MODE_DESKTOP_SWITCH
#define TOUCHBAR_MODE_REMOTE HW75_TOUCHBAR_MODE_REMOTE
#define TOUCHBAR_MODE_COUNT HW75_TOUCHBAR_MODE_COUNT
#define TOUCHBAR_MODE_INDICATOR_DURATION_MS 1200U

/* REMOTE-mode gesture classification thresholds. */
#define TOUCHBAR_REMOTE_SWIPE_DISTANCE 96
#define TOUCHBAR_REMOTE_LONG_MS 450U

struct touchbar_session {
    enum hw75_touchbar_mode mode;
    bool is_touching;
    bool is_gesture_active;
    bool is_desktop_seek_mode;
    bool is_no_touch_pending;
    uint8_t active_segment;
    uint8_t active_touch_count;
    uint32_t touch_start_ms;
    uint32_t last_touch_ms;
    uint32_t last_pan_ms;
    uint32_t last_step_ms;
    uint32_t edge_hold_start_ms;
    uint32_t app_switch_release_guard_until_ms;
    int16_t anchor_position;
    int16_t step_anchor_position;
    int16_t current_position;
    int16_t emitted_steps;
    int8_t edge_hold_direction;
    int8_t step_entry_edge_direction;
    uint32_t trace_id;
    bool logged_activation_wait;
    bool logged_release_grace;
    bool logged_desktop_hold;
    bool logged_app_release_guard;
};

struct touchbar_key_pulse {
    uint32_t encoded;
    uint8_t delay_ticks;
    uint8_t remaining_ticks;
    bool pressed;
};

struct touchbar_synthetic_state {
    bool hold_left_alt;
    bool hold_left_shift;
    bool left_alt_active;
    bool left_shift_active;
    bool shift_scroll_primed;
    struct touchbar_key_pulse left_shift_pulse;
    struct touchbar_key_pulse left_ctrl_pulse;
    struct touchbar_key_pulse left_gui_pulse;
    struct touchbar_key_pulse tab_pulse;
    struct touchbar_key_pulse left_arrow_pulse;
    struct touchbar_key_pulse right_arrow_pulse;
};

static uint8_t touchbar_segment_touch_map[TOUCHBAR_SEGMENT_CAPACITY][TOUCHBAR_TOUCH_MAP_CAPACITY] = {
    {0, 1, 2, 3},
    {2, 3, 4, 5},
};

static uint8_t touchbar_segment_touch_count[TOUCHBAR_SEGMENT_CAPACITY] = {4, 4};

static uint8_t touchbar_segment_entry_touch_map[TOUCHBAR_SEGMENT_CAPACITY][TOUCHBAR_ENTRY_MAP_CAPACITY] = {
    {0, 1, 2},
    {3, 4, 5},
};

static uint8_t touchbar_segment_entry_touch_count[TOUCHBAR_SEGMENT_CAPACITY] = {3, 3};
static uint8_t touchbar_segment_count = 2U;

static struct hw75_touchbar_pan_config touchbar_pan_config = {
    .activation_ms = TOUCHBAR_ACTIVATION_MS,
    .release_grace_ms = TOUCHBAR_RELEASE_GRACE_MS,
    .poll_interval_ms = TOUCHBAR_POLL_INTERVAL_MS,
    .interval_ms = TOUCHBAR_PAN_INTERVAL_MS,
    .deadzone = TOUCHBAR_PAN_DEADZONE,
    .position_scale = TOUCHBAR_POSITION_SCALE,
};

static struct hw75_touchbar_app_switch_config touchbar_app_switch_config = {
    .activation_ms = TOUCHBAR_APP_ACTIVATION_MS,
    .release_grace_ms = TOUCHBAR_SWITCH_RELEASE_GRACE_MS,
    .release_settle_ms = TOUCHBAR_APP_RELEASE_SETTLE_MS,
    .step_interval_ms = TOUCHBAR_APP_STEP_INTERVAL_MS,
    .step_distance = TOUCHBAR_STEP_DISTANCE,
    .edge_repeat_delay_ms = TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS,
};

static struct hw75_touchbar_desktop_switch_config touchbar_desktop_switch_config = {
    .activation_ms = TOUCHBAR_ACTIVATION_MS,
    .release_grace_ms = TOUCHBAR_SWITCH_RELEASE_GRACE_MS,
    .hold_ms = TOUCHBAR_DESKTOP_HOLD_MS,
    .step_interval_ms = TOUCHBAR_DESKTOP_STEP_INTERVAL_MS,
    .step_distance = TOUCHBAR_DESKTOP_STEP_DISTANCE,
    .edge_repeat_delay_ms = TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS,
    .swipe_distance = TOUCHBAR_DESKTOP_SWIPE_DISTANCE,
};

static bool touchbar_mode_indicator_enabled = true;

static struct hw75_touchbar_mode_indicator
    touchbar_mode_indicators[HW75_TOUCHBAR_MODE_COUNT] = {
        [HW75_TOUCHBAR_MODE_PAN] =
            {
                .red = 0x20,
                .green = 0xD0,
                .blue = 0x50,
                .duration_ms = TOUCHBAR_MODE_INDICATOR_DURATION_MS,
            },
        [HW75_TOUCHBAR_MODE_APP_SWITCH] =
            {
                .red = 0xE0,
                .green = 0x90,
                .blue = 0x10,
                .duration_ms = TOUCHBAR_MODE_INDICATOR_DURATION_MS,
            },
        [HW75_TOUCHBAR_MODE_DESKTOP_SWITCH] =
            {
                .red = 0x20,
                .green = 0x70,
                .blue = 0xE0,
                .duration_ms = TOUCHBAR_MODE_INDICATOR_DURATION_MS,
            },
        [HW75_TOUCHBAR_MODE_REMOTE] =
            {
                .red = 0x90,
                .green = 0x20,
                .blue = 0xE0,
                .duration_ms = TOUCHBAR_MODE_INDICATOR_DURATION_MS,
            },
};

static const struct device *touchbar_kscan_dev;
static struct touchbar_session touchbar = {.mode = TOUCHBAR_MODE_PAN, .active_segment = TOUCHBAR_INVALID_SEGMENT};
static struct touchbar_synthetic_state synthetic;
static struct k_work_delayable touchbar_poll_work;
static int touchbar_last_poll_err;

static uint8_t touchbar_mask_from_positions(const uint8_t *positions, uint8_t count);
static int16_t touchbar_abs16(int16_t value);

static uint16_t touchbar_position_scale(void) {
    return touchbar_pan_config.position_scale == 0U ? 1U : touchbar_pan_config.position_scale;
}

static uint16_t touchbar_pan_interval_ms(void) {
    return touchbar_pan_config.interval_ms == 0U ? 1U : touchbar_pan_config.interval_ms;
}

static uint16_t touchbar_poll_interval_ms(void) {
    return touchbar_pan_config.poll_interval_ms == 0U ? 1U : touchbar_pan_config.poll_interval_ms;
}

static uint16_t touchbar_app_step_interval_ms(void) {
    return touchbar_app_switch_config.step_interval_ms == 0U ? 1U
                                                             : touchbar_app_switch_config.step_interval_ms;
}

static uint16_t touchbar_desktop_step_interval_ms(void) {
    return touchbar_desktop_switch_config.step_interval_ms == 0U
               ? 1U
               : touchbar_desktop_switch_config.step_interval_ms;
}

static uint16_t touchbar_app_step_distance(void) {
    return touchbar_app_switch_config.step_distance == 0U ? 1U : touchbar_app_switch_config.step_distance;
}

static uint16_t touchbar_desktop_step_distance(void) {
    return touchbar_desktop_switch_config.step_distance == 0U ? 1U
                                                              : touchbar_desktop_switch_config.step_distance;
}

static uint8_t touchbar_diag_mode(enum hw75_touchbar_mode mode) { return (uint8_t)mode; }

static void touchbar_preview_mode_indicator(enum hw75_touchbar_mode mode) {
    if (!touchbar_mode_indicator_enabled) {
        return;
    }

    if ((uint32_t)mode >= ARRAY_SIZE(touchbar_mode_indicators)) {
        return;
    }

    const struct hw75_touchbar_mode_indicator *indicator = &touchbar_mode_indicators[mode];
    indicator_preview_rgb(indicator->red, indicator->green, indicator->blue, indicator->duration_ms);
}

static uint8_t touchbar_diag_phase_code(enum hw75_diag_touchbar_phase phase) {
    return (uint8_t)phase;
}

static uint8_t touchbar_diag_flags(void) {
    uint8_t flags = 0U;

    if (touchbar.is_touching) {
        flags |= BIT(0);
    }
    if (touchbar.is_gesture_active) {
        flags |= BIT(1);
    }
    if (touchbar.is_desktop_seek_mode) {
        flags |= BIT(2);
    }
    if (touchbar.is_no_touch_pending) {
        flags |= BIT(3);
    }

    return flags;
}

static uint32_t touchbar_diag_touch_meta(uint8_t touch_state, uint8_t phase) {
    return hw75_diag_pack_u8x4(touchbar_diag_mode(touchbar.mode), phase, touch_state,
                               touchbar_diag_flags());
}

static void touchbar_update_snapshot(uint8_t touch_state, enum hw75_diag_touchbar_phase phase) {
    hw75_diag_update_snapshot(
        HW75_DIAG_MODULE_TOUCHBAR, touchbar_diag_touch_meta(touch_state, touchbar_diag_phase_code(phase)),
        hw75_diag_pack_u8x4(touchbar.active_segment, touchbar.active_touch_count, 0U, 0U),
        hw75_diag_pack_s16x2(touchbar.current_position, touchbar.emitted_steps));
}

static uint8_t touchbar_shared_point_count(void) {
    uint8_t memberships[HW75_TOUCHBAR_CHANNEL_COUNT] = {0};

    for (uint8_t segment_index = 0U; segment_index < touchbar_segment_count; segment_index++) {
        for (uint8_t point_index = 0U; point_index < touchbar_segment_touch_count[segment_index];
             point_index++) {
            uint8_t logical_position = touchbar_segment_touch_map[segment_index][point_index];
            if (logical_position < HW75_TOUCHBAR_CHANNEL_COUNT) {
                memberships[logical_position]++;
            }
        }
    }

    uint8_t shared_count = 0U;
    for (uint8_t logical_position = 0U; logical_position < HW75_TOUCHBAR_CHANNEL_COUNT;
         logical_position++) {
        if (memberships[logical_position] > 1U) {
            shared_count++;
        }
    }

    return shared_count;
}

static int touchbar_emit_key_event(uint32_t encoded, bool pressed, int64_t timestamp) {
    return ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(encoded, pressed, timestamp));
}

static int touchbar_set_hold(bool *active, uint32_t encoded, bool hold, int64_t timestamp) {
    if (*active == hold) {
        return 0;
    }

    *active = hold;
    return touchbar_emit_key_event(encoded, hold, timestamp);
}

static void touchbar_schedule_pulse(struct touchbar_key_pulse *pulse, uint32_t encoded,
                                    uint8_t delay_ticks, uint8_t remaining_ticks,
                                    int64_t timestamp) {
    if (pulse->pressed) {
        touchbar_emit_key_event(pulse->encoded, false, timestamp);
    }

    *pulse = (struct touchbar_key_pulse){
        .encoded = encoded,
        .delay_ticks = delay_ticks,
        .remaining_ticks = remaining_ticks,
        .pressed = false,
    };
}

static void touchbar_tick_pulse(struct touchbar_key_pulse *pulse, int64_t timestamp) {
    if (pulse->remaining_ticks == 0U && !pulse->pressed) {
        return;
    }

    if (pulse->delay_ticks > 0U) {
        pulse->delay_ticks--;
        if (pulse->delay_ticks == 0U) {
            touchbar_emit_key_event(pulse->encoded, true, timestamp);
            pulse->pressed = true;
        }
        return;
    }

    if (!pulse->pressed) {
        touchbar_emit_key_event(pulse->encoded, true, timestamp);
        pulse->pressed = true;
    }

    if (pulse->remaining_ticks > 0U) {
        pulse->remaining_ticks--;
        if (pulse->remaining_ticks == 0U) {
            touchbar_emit_key_event(pulse->encoded, false, timestamp);
            pulse->pressed = false;
        }
    }
}

static uint8_t touchbar_logical_bit(uint8_t logical_position) {
    return (uint8_t)(1U << (6U - 1U - logical_position));
}

static uint8_t touchbar_count_mapped_touches(uint8_t touch_state, const uint8_t *logical_positions,
                                             uint8_t logical_count) {
    uint8_t touch_count = 0U;

    for (uint8_t index = 0; index < logical_count; index++) {
        if ((touch_state & touchbar_logical_bit(logical_positions[index])) != 0U) {
            touch_count++;
        }
    }

    return touch_count;
}

static int16_t touchbar_mapped_position(uint8_t touch_state, const uint8_t *logical_positions,
                                        uint8_t logical_count) {
    uint16_t weighted_sum = 0U;
    uint8_t active_count = 0U;

    for (uint8_t index = 0; index < logical_count; index++) {
        if ((touch_state & touchbar_logical_bit(logical_positions[index])) != 0U) {
            weighted_sum += (uint16_t)index * touchbar_position_scale();
            active_count++;
        }
    }

    if (active_count == 0U) {
        return -1;
    }

    return (int16_t)(weighted_sum / active_count);
}

static uint8_t touchbar_count_entry_touches(uint8_t touch_state, uint8_t segment_index) {
    return touchbar_count_mapped_touches(touch_state, touchbar_segment_entry_touch_map[segment_index],
                                         touchbar_segment_entry_touch_count[segment_index]);
}

static uint8_t touchbar_count_segment_touches(uint8_t touch_state, uint8_t segment_index) {
    return touchbar_count_mapped_touches(touch_state, touchbar_segment_touch_map[segment_index],
                                         touchbar_segment_touch_count[segment_index]);
}

static int16_t touchbar_segment_position(uint8_t touch_state, uint8_t segment_index) {
    return touchbar_mapped_position(touch_state, touchbar_segment_touch_map[segment_index],
                                    touchbar_segment_touch_count[segment_index]);
}

static int16_t touchbar_global_position(uint8_t touch_state) {
    static const uint8_t logical_positions[] = {0, 1, 2, 3, 4, 5};
    return touchbar_mapped_position(touch_state, logical_positions, ARRAY_SIZE(logical_positions));
}

static uint8_t touchbar_segment_point_count(uint8_t segment_index) {
    if (segment_index >= touchbar_segment_count) {
        return 0U;
    }

    return touchbar_segment_touch_count[segment_index];
}

static int16_t touchbar_segment_center_global_position(uint8_t segment_index) {
    if (segment_index >= touchbar_segment_count) {
        return -1;
    }

    uint16_t weighted_sum = 0U;
    uint8_t count = touchbar_segment_touch_count[segment_index];
    if (count == 0U) {
        return -1;
    }

    for (uint8_t point_index = 0U; point_index < count; point_index++) {
        weighted_sum +=
            (uint16_t)touchbar_segment_touch_map[segment_index][point_index] * touchbar_position_scale();
    }

    return (int16_t)(weighted_sum / count);
}

static uint8_t touchbar_select_segment(uint8_t touch_state) {
    uint8_t best_segment = TOUCHBAR_INVALID_SEGMENT;
    uint8_t best_entry_touches = 0U;

    for (uint8_t segment_index = 0U; segment_index < touchbar_segment_count; segment_index++) {
        uint8_t entry_touches = touchbar_count_entry_touches(touch_state, segment_index);
        if (entry_touches > best_entry_touches) {
            best_entry_touches = entry_touches;
            best_segment = segment_index;
        }
    }

    if (best_entry_touches == 0U) {
        return TOUCHBAR_INVALID_SEGMENT;
    }

    int16_t global_position = touchbar_global_position(touch_state);
    if (global_position < 0) {
        return best_segment;
    }

    int16_t best_distance = INT16_MAX;
    for (uint8_t segment_index = 0U; segment_index < touchbar_segment_count; segment_index++) {
        uint8_t entry_touches = touchbar_count_entry_touches(touch_state, segment_index);
        if (entry_touches != best_entry_touches) {
            continue;
        }

        int16_t center = touchbar_segment_center_global_position(segment_index);
        int16_t distance = touchbar_abs16(global_position - center);
        if (distance < best_distance) {
            best_distance = distance;
            best_segment = segment_index;
        }
    }

    return best_segment;
}

static uint32_t touchbar_activation_delay_ms(void) {
    return touchbar.mode == TOUCHBAR_MODE_APP_SWITCH ? touchbar_app_switch_config.activation_ms
                                                     : touchbar_pan_config.activation_ms;
}

static uint32_t touchbar_release_grace_ms(void) {
    switch (touchbar.mode) {
    case TOUCHBAR_MODE_APP_SWITCH:
        return touchbar_app_switch_config.release_grace_ms;
    case TOUCHBAR_MODE_DESKTOP_SWITCH:
        return touchbar_desktop_switch_config.release_grace_ms;
    default:
        return touchbar_pan_config.release_grace_ms;
    }
}

static int16_t touchbar_abs16(int16_t value) { return value >= 0 ? value : (int16_t)-value; }

static int16_t touchbar_segment_max_position(uint8_t segment_index) {
    uint8_t point_count = touchbar_segment_point_count(segment_index);
    if (point_count <= 1U) {
        return 0;
    }

    return (int16_t)((point_count - 1U) * touchbar_position_scale());
}

static int16_t touchbar_edge_direction(int16_t position) {
    int16_t max_position = touchbar_segment_max_position(touchbar.active_segment);
    if (position <= TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
        return -1;
    }
    if (position >= max_position - TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
        return 1;
    }
    return 0;
}

static int16_t touchbar_step_direction_from_displacement(int16_t displacement,
                                                         int16_t step_distance) {
    if (displacement >= step_distance) {
        return 1;
    }
    if (displacement <= -step_distance) {
        return -1;
    }
    return 0;
}

static bool touchbar_is_within_entry_edge_lock(int16_t position, int16_t edge_direction) {
    int16_t max_position = touchbar_segment_max_position(touchbar.active_segment);
    int16_t release_threshold = touchbar_position_scale() + TOUCHBAR_EDGE_REPEAT_THRESHOLD;

    if (edge_direction < 0) {
        return position <= release_threshold;
    }
    if (edge_direction > 0) {
        return position >= max_position - release_threshold;
    }

    return false;
}

static bool touchbar_step_entry_allows_direction(int16_t direction) {
    if (direction == 0 || touchbar.step_entry_edge_direction == 0) {
        return true;
    }
    if (direction == touchbar.step_entry_edge_direction) {
        return true;
    }
    if (!touchbar_is_within_entry_edge_lock(touchbar.current_position,
                                            touchbar.step_entry_edge_direction)) {
        touchbar.step_entry_edge_direction = 0;
        return true;
    }

    return false;
}

static void touchbar_reset_edge_hold(void) {
    touchbar.edge_hold_start_ms = 0U;
    touchbar.edge_hold_direction = 0;
}

static void touchbar_arm_edge_hold(uint32_t now_ms, int16_t edge_direction) {
    touchbar.edge_hold_direction = (int8_t)edge_direction;
    touchbar.edge_hold_start_ms = now_ms;
}

static void touchbar_release_holds(int64_t timestamp) {
    touchbar_set_hold(&synthetic.left_alt_active, LEFT_ALT, false, timestamp);
    touchbar_set_hold(&synthetic.left_shift_active, LEFT_SHIFT, false, timestamp);
    synthetic.hold_left_alt = false;
    synthetic.hold_left_shift = false;
    synthetic.shift_scroll_primed = false;
}

static void touchbar_cancel_synthetic(int64_t timestamp) {
    touchbar_release_holds(timestamp);
    touchbar_schedule_pulse(&synthetic.left_shift_pulse, LEFT_SHIFT, 0U, 0U, timestamp);
    touchbar_schedule_pulse(&synthetic.left_ctrl_pulse, LEFT_CONTROL, 0U, 0U, timestamp);
    touchbar_schedule_pulse(&synthetic.left_gui_pulse, LEFT_GUI, 0U, 0U, timestamp);
    touchbar_schedule_pulse(&synthetic.tab_pulse, TAB, 0U, 0U, timestamp);
    touchbar_schedule_pulse(&synthetic.left_arrow_pulse, LEFT_ARROW, 0U, 0U, timestamp);
    touchbar_schedule_pulse(&synthetic.right_arrow_pulse, RIGHT_ARROW, 0U, 0U, timestamp);

    memset(&synthetic, 0, sizeof(synthetic));
}

static void touchbar_clear_actions(int64_t timestamp) {
    touchbar.is_touching = false;
    touchbar.is_gesture_active = false;
    touchbar.is_desktop_seek_mode = false;
    touchbar.is_no_touch_pending = false;
    touchbar.active_segment = TOUCHBAR_INVALID_SEGMENT;
    touchbar.active_touch_count = 0U;
    touchbar.touch_start_ms = 0U;
    touchbar.last_touch_ms = 0U;
    touchbar.last_pan_ms = 0U;
    touchbar.last_step_ms = 0U;
    touchbar.edge_hold_start_ms = 0U;
    touchbar.app_switch_release_guard_until_ms = 0U;
    touchbar.anchor_position = 0;
    touchbar.step_anchor_position = 0;
    touchbar.current_position = 0;
    touchbar.emitted_steps = 0;
    touchbar.edge_hold_direction = 0;
    touchbar.step_entry_edge_direction = 0;
    touchbar.trace_id = 0U;
    touchbar.logged_activation_wait = false;
    touchbar.logged_release_grace = false;
    touchbar.logged_desktop_hold = false;
    touchbar.logged_app_release_guard = false;

    touchbar_release_holds(timestamp);
}

static void touchbar_queue_mouse_wheel(int8_t wheel) {
    if (wheel == 0) {
        return;
    }

    hid_mouse_wheel_report(wheel, true);
    hid_mouse_wheel_report(wheel, false);
    hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                        HW75_DIAG_EVENT_TOUCHBAR_PAN_WHEEL,
                        hw75_diag_pack_u8x4((uint8_t)wheel, 0U, 0U, 0U), false, 0U);
}

static void touchbar_queue_app_switch_step(int16_t direction, int64_t timestamp) {
    synthetic.hold_left_alt = true;
    touchbar_schedule_pulse(&synthetic.tab_pulse, TAB, 1U, TOUCHBAR_PULSE_TICKS, timestamp);
    if (direction < 0) {
        touchbar_schedule_pulse(&synthetic.left_shift_pulse, LEFT_SHIFT, 0U,
                                TOUCHBAR_PULSE_TICKS + 1U, timestamp);
    }

    hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                        HW75_DIAG_EVENT_TOUCHBAR_APP_SWITCH_STEP,
                        hw75_diag_pack_u8x4((uint8_t)direction, 0U, 0U, 0U), false, 0U);
}

static void touchbar_queue_desktop_switch_step(int16_t direction, int64_t timestamp) {
    touchbar_schedule_pulse(&synthetic.left_ctrl_pulse, LEFT_CONTROL, 0U,
                            TOUCHBAR_PULSE_TICKS + 1U, timestamp);
    touchbar_schedule_pulse(&synthetic.left_gui_pulse, LEFT_GUI, 0U,
                            TOUCHBAR_PULSE_TICKS + 1U, timestamp);

    if (direction < 0) {
        touchbar_schedule_pulse(&synthetic.left_arrow_pulse, LEFT_ARROW, 1U, TOUCHBAR_PULSE_TICKS,
                                timestamp);
    } else {
        touchbar_schedule_pulse(&synthetic.right_arrow_pulse, RIGHT_ARROW, 1U,
                                TOUCHBAR_PULSE_TICKS, timestamp);
    }

    hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                        HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_SWITCH_STEP,
                        hw75_diag_pack_u8x4((uint8_t)direction, 0U, 0U, 0U), false, 0U);
}

static bool touchbar_try_repeat_step_at_edge(uint32_t now_ms, uint32_t hold_delay_ms,
                                             uint32_t step_interval_ms, int16_t step_distance,
                                             int64_t timestamp,
                                             void (*queue_step)(int16_t direction,
                                                                int64_t timestamp)) {
    int16_t edge_direction = touchbar_edge_direction(touchbar.current_position);
    if (edge_direction == 0) {
        touchbar_reset_edge_hold();
        return false;
    }

    if (touchbar.edge_hold_direction != edge_direction) {
        touchbar_arm_edge_hold(now_ms, edge_direction);
        hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR,
                            touchbar.trace_id, HW75_DIAG_EVENT_TOUCHBAR_EDGE_HOLD_ARM,
                            hw75_diag_pack_u8x4((uint8_t)edge_direction, 0U, 0U, 0U), true,
                            (uint16_t)touchbar.current_position);
        return true;
    }
    if (now_ms - touchbar.edge_hold_start_ms < hold_delay_ms) {
        return true;
    }
    if (now_ms - touchbar.last_step_ms < step_interval_ms) {
        return true;
    }

    touchbar.last_step_ms = now_ms;
    queue_step(edge_direction, timestamp);
    touchbar.emitted_steps += edge_direction;
    touchbar.step_anchor_position -= edge_direction * step_distance;
    return true;
}

static bool touchbar_should_delay_desktop_edge_continuation(uint32_t now_ms,
                                                            int16_t pending_direction) {
    int16_t edge_direction = touchbar_edge_direction(touchbar.current_position);
    if (edge_direction == 0) {
        touchbar_reset_edge_hold();
        return false;
    }
    if (pending_direction != edge_direction) {
        return false;
    }

    if (touchbar.edge_hold_direction != edge_direction) {
        touchbar_arm_edge_hold(now_ms, edge_direction);
        hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR,
                            touchbar.trace_id, HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_EDGE_ARM,
                            hw75_diag_pack_u8x4((uint8_t)edge_direction, 0U, 0U, 0U), true,
                            (uint16_t)touchbar.current_position);
        return false;
    }

    return now_ms - touchbar.edge_hold_start_ms < touchbar_desktop_switch_config.edge_repeat_delay_ms;
}

static void touchbar_handle_pan_mode(uint32_t now_ms) {
    if (now_ms - touchbar.last_pan_ms < touchbar_pan_interval_ms()) {
        return;
    }

    touchbar.last_pan_ms = now_ms;

    int16_t displacement = touchbar.current_position - touchbar.anchor_position;
    int16_t distance = touchbar_abs16(displacement);
    if (distance <= touchbar_pan_config.deadzone) {
        return;
    }

    int16_t position_scale = (int16_t)touchbar_position_scale();
    int16_t speed = 1 + (distance - touchbar_pan_config.deadzone) / MAX(1, position_scale / 2);
    if (speed > 6) {
        speed = 6;
    }

    synthetic.hold_left_shift = true;
    if (!synthetic.shift_scroll_primed) {
        synthetic.shift_scroll_primed = true;
        return;
    }

    touchbar_queue_mouse_wheel((int8_t)(displacement > 0 ? -speed : speed));
}

static void touchbar_handle_app_switch_mode(uint32_t now_ms, int64_t timestamp) {
    if (now_ms < touchbar.app_switch_release_guard_until_ms) {
        if (!touchbar.logged_app_release_guard) {
            touchbar.logged_app_release_guard = true;
            hw75_diag_log_event(
                HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                HW75_DIAG_EVENT_TOUCHBAR_APP_RELEASE_GUARD,
                touchbar.app_switch_release_guard_until_ms - now_ms, false, 0U);
        }
        return;
    }
    touchbar.logged_app_release_guard = false;

    int16_t displacement = touchbar.current_position - touchbar.step_anchor_position;
    int16_t step_direction =
        touchbar_step_direction_from_displacement(displacement, touchbar_app_step_distance());

    if (step_direction == 0) {
        touchbar_try_repeat_step_at_edge(now_ms, touchbar_app_switch_config.edge_repeat_delay_ms,
                                         touchbar_app_step_interval_ms(), touchbar_app_step_distance(),
                                         timestamp, touchbar_queue_app_switch_step);
        return;
    }
    if (!touchbar_step_entry_allows_direction(step_direction)) {
        return;
    }
    if (now_ms - touchbar.last_step_ms < touchbar_app_step_interval_ms()) {
        return;
    }

    touchbar.last_step_ms = now_ms;
    if (touchbar_edge_direction(touchbar.current_position) == 0) {
        touchbar_reset_edge_hold();
    }
    touchbar_queue_app_switch_step(step_direction, timestamp);
    touchbar.emitted_steps += step_direction;
    touchbar.step_anchor_position += step_direction * touchbar_app_step_distance();
}

static void touchbar_finalize_desktop_gesture(int64_t timestamp) {
    if (!touchbar.is_gesture_active || touchbar.is_desktop_seek_mode) {
        return;
    }

    int16_t displacement = touchbar.current_position - touchbar.anchor_position;
    if (touchbar_abs16(displacement) < touchbar_desktop_switch_config.swipe_distance) {
        return;
    }

    touchbar_queue_desktop_switch_step(displacement > 0 ? 1 : -1, timestamp);
    hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                        HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_FINALIZE, (uint16_t)displacement,
                        false, 0U);
}

static void touchbar_handle_desktop_switch_mode(uint32_t now_ms, int64_t timestamp) {
    if (!touchbar.is_desktop_seek_mode) {
        if (now_ms - touchbar.touch_start_ms < touchbar_desktop_switch_config.hold_ms) {
            if (!touchbar.logged_desktop_hold) {
                touchbar.logged_desktop_hold = true;
                hw75_diag_log_event(
                    HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                    HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_HOLD_WAIT,
                    hw75_diag_pack_u16x2((uint16_t)(now_ms - touchbar.touch_start_ms),
                                         touchbar_desktop_switch_config.hold_ms),
                    false, 0U);
            }
            return;
        }

        touchbar.logged_desktop_hold = false;
        touchbar.is_desktop_seek_mode = true;
        touchbar.step_anchor_position = touchbar.current_position;
        touchbar.emitted_steps = 0;
        touchbar.step_entry_edge_direction = touchbar_edge_direction(touchbar.current_position);
        touchbar_reset_edge_hold();
        touchbar.last_step_ms = now_ms - touchbar_desktop_step_interval_ms();
        hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR,
                            touchbar.trace_id, HW75_DIAG_EVENT_TOUCHBAR_DESKTOP_SEEK,
                            (uint16_t)touchbar.step_anchor_position, false, 0U);
        return;
    }

    int16_t displacement = touchbar.current_position - touchbar.step_anchor_position;
    int16_t step_direction = touchbar_step_direction_from_displacement(
        displacement, touchbar_desktop_step_distance());

    if (step_direction == 0) {
        touchbar_try_repeat_step_at_edge(now_ms, touchbar_desktop_switch_config.edge_repeat_delay_ms,
                                         touchbar_desktop_step_interval_ms(),
                                         touchbar_desktop_step_distance(), timestamp,
                                         touchbar_queue_desktop_switch_step);
        return;
    }
    if (!touchbar_step_entry_allows_direction(step_direction)) {
        return;
    }
    if (touchbar_should_delay_desktop_edge_continuation(now_ms, step_direction)) {
        return;
    }
    if (now_ms - touchbar.last_step_ms < touchbar_desktop_step_interval_ms()) {
        return;
    }

    touchbar.last_step_ms = now_ms;
    if (touchbar_edge_direction(touchbar.current_position) == 0) {
        touchbar_reset_edge_hold();
    }
    touchbar_queue_desktop_switch_step(step_direction, timestamp);
    touchbar.emitted_steps += step_direction;
    touchbar.step_anchor_position += step_direction * touchbar_desktop_step_distance();
}

static void touchbar_apply_holds(int64_t timestamp) {
    touchbar_set_hold(&synthetic.left_alt_active, LEFT_ALT, synthetic.hold_left_alt, timestamp);
    touchbar_set_hold(&synthetic.left_shift_active, LEFT_SHIFT, synthetic.hold_left_shift, timestamp);
}

static void touchbar_tick_pulses(int64_t timestamp) {
    touchbar_tick_pulse(&synthetic.left_shift_pulse, timestamp);
    touchbar_tick_pulse(&synthetic.left_ctrl_pulse, timestamp);
    touchbar_tick_pulse(&synthetic.left_gui_pulse, timestamp);
    touchbar_tick_pulse(&synthetic.tab_pulse, timestamp);
    touchbar_tick_pulse(&synthetic.left_arrow_pulse, timestamp);
    touchbar_tick_pulse(&synthetic.right_arrow_pulse, timestamp);
}

/*
 * REMOTE mode: instead of emitting HID, the strip becomes a remote for the
 * dynamic module. The actual forwarding is provided (strongly) by the keyboard
 * uart_comm; this weak default keeps the shared touchbar.c board-agnostic.
 */
__weak void hw75_touchbar_remote_gesture(uint8_t verb) { ARG_UNUSED(verb); }

static void touchbar_finalize_remote_gesture(void) {
    int16_t disp = (int16_t)(touchbar.current_position - touchbar.anchor_position);
    uint32_t dur = touchbar.last_touch_ms - touchbar.touch_start_ms;
    uint8_t verb;

    if (touchbar_abs16(disp) >= TOUCHBAR_REMOTE_SWIPE_DISTANCE) {
        verb = disp > 0 ? HW75_TOUCHBAR_GESTURE_SWIPE_R : HW75_TOUCHBAR_GESTURE_SWIPE_L;
    } else if (dur >= TOUCHBAR_REMOTE_LONG_MS) {
        verb = HW75_TOUCHBAR_GESTURE_LONG;
    } else {
        verb = HW75_TOUCHBAR_GESTURE_TAP;
    }

    hw75_touchbar_remote_gesture(verb);
}

static void touchbar_process(uint8_t touch_state, uint32_t now_ms, int64_t timestamp) {
    uint8_t active_segment = TOUCHBAR_INVALID_SEGMENT;
    uint8_t active_touch_count = 0U;
    int16_t touch_position = -1;

    if (touchbar.is_touching) {
        active_segment = touchbar.active_segment;
        if (active_segment < touchbar_segment_count) {
            touch_position = touchbar_segment_position(touch_state, active_segment);
            active_touch_count = touchbar_count_segment_touches(touch_state, active_segment);
        }
    } else {
        active_segment = touchbar_select_segment(touch_state);
        if (active_segment < touchbar_segment_count) {
            touch_position = touchbar_segment_position(touch_state, active_segment);
            active_touch_count = touchbar_count_segment_touches(touch_state, active_segment);
        }
    }

    if (touch_position < 0) {
        if (!touchbar.is_touching) {
            touchbar_apply_holds(timestamp);
            touchbar_tick_pulses(timestamp);
            return;
        }

        if (!touchbar.is_no_touch_pending) {
            touchbar.is_no_touch_pending = true;
            touchbar.last_touch_ms = now_ms;
            hw75_diag_log_event(
                HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                HW75_DIAG_EVENT_TOUCHBAR_RELEASE_PENDING,
                hw75_diag_pack_u8x4(touchbar_diag_mode(touchbar.mode), touch_state, 0U, 0U),
                true, touchbar_release_grace_ms());
            touchbar_apply_holds(timestamp);
            touchbar_tick_pulses(timestamp);
            return;
        }

        if (now_ms - touchbar.last_touch_ms < touchbar_release_grace_ms()) {
            if (!touchbar.logged_release_grace) {
                touchbar.logged_release_grace = true;
                hw75_diag_log_event(
                    HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                    HW75_DIAG_EVENT_TOUCHBAR_RELEASE_GRACE,
                    hw75_diag_pack_u16x2((uint16_t)(now_ms - touchbar.last_touch_ms),
                                         (uint16_t)touchbar_release_grace_ms()),
                    true, touch_state);
            }
            touchbar_apply_holds(timestamp);
            touchbar_tick_pulses(timestamp);
            return;
        }
        touchbar.logged_release_grace = false;

        if (touchbar.mode == TOUCHBAR_MODE_DESKTOP_SWITCH) {
            touchbar_finalize_desktop_gesture(timestamp);
        } else if (touchbar.mode == TOUCHBAR_MODE_REMOTE) {
            touchbar_finalize_remote_gesture();
        }

        touchbar_apply_holds(timestamp);
        touchbar_tick_pulses(timestamp);
        hw75_diag_log_event(
            HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
            HW75_DIAG_EVENT_TOUCHBAR_TOUCH_END,
            hw75_diag_pack_u8x4(touchbar_diag_mode(touchbar.mode), touch_state,
                                touchbar.active_segment, touchbar.active_touch_count),
            false, 0U);
        touchbar_clear_actions(timestamp);
        touchbar_update_snapshot(touch_state, HW75_DIAG_TOUCHBAR_PHASE_RELEASED);
        return;
    }

    touchbar.is_no_touch_pending = false;
    touchbar.logged_release_grace = false;
    touchbar.last_touch_ms = now_ms;
    touchbar.current_position = touch_position;

    if (touchbar.mode == TOUCHBAR_MODE_APP_SWITCH) {
        bool release_order_jitter = touchbar.is_gesture_active &&
                                    touchbar.active_touch_count > active_touch_count &&
                                    active_touch_count == 1U;
        if (release_order_jitter) {
            touchbar.app_switch_release_guard_until_ms =
                now_ms + touchbar_app_switch_config.release_settle_ms;
            hw75_diag_log_event(
                HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                HW75_DIAG_EVENT_TOUCHBAR_APP_RELEASE_JITTER,
                touchbar_app_switch_config.release_settle_ms,
                true, touch_state);
        } else if (active_touch_count > 1U) {
            touchbar.app_switch_release_guard_until_ms = 0U;
        }
    }

    if (!touchbar.is_touching) {
        touchbar.trace_id = hw75_diag_next_trace_id();
        touchbar.is_touching = true;
        touchbar.active_segment = active_segment;
        touchbar.active_touch_count = active_touch_count;
        touchbar.touch_start_ms = now_ms;
        touchbar.last_touch_ms = now_ms;
        touchbar.anchor_position = touch_position;
        touchbar.step_anchor_position = touch_position;
        touchbar.current_position = touch_position;
        touchbar.emitted_steps = 0;
        touchbar.is_desktop_seek_mode = false;
        touchbar.last_pan_ms = now_ms;
        touchbar.last_step_ms = now_ms;
        touchbar_reset_edge_hold();
        touchbar.step_entry_edge_direction = touchbar_edge_direction(touch_position);
        touchbar.app_switch_release_guard_until_ms = 0U;
        synthetic.shift_scroll_primed = false;
        touchbar.logged_activation_wait = false;
        touchbar.logged_desktop_hold = false;
        touchbar.logged_app_release_guard = false;
        touchbar.logged_release_grace = false;
        hw75_diag_log_event(
            HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
            HW75_DIAG_EVENT_TOUCHBAR_TOUCH_START,
            hw75_diag_pack_u8x4(touchbar_diag_mode(touchbar.mode), touch_state, active_segment,
                                active_touch_count),
            true, (uint16_t)touch_position);
        touchbar_update_snapshot(touch_state, HW75_DIAG_TOUCHBAR_PHASE_TOUCH_START);
        touchbar_apply_holds(timestamp);
        touchbar_tick_pulses(timestamp);
        return;
    }

    touchbar.active_touch_count = active_touch_count;

    if (!touchbar.is_gesture_active) {
        if (now_ms - touchbar.touch_start_ms < touchbar_activation_delay_ms()) {
            if (!touchbar.logged_activation_wait) {
                touchbar.logged_activation_wait = true;
                hw75_diag_log_event(
                    HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
                    HW75_DIAG_EVENT_TOUCHBAR_ACTIVATION_WAIT,
                    hw75_diag_pack_u16x2((uint16_t)(now_ms - touchbar.touch_start_ms),
                                         (uint16_t)touchbar_activation_delay_ms()),
                    true, touch_state);
            }
            touchbar_apply_holds(timestamp);
            touchbar_tick_pulses(timestamp);
            return;
        }

        touchbar.is_gesture_active = true;
        touchbar.logged_activation_wait = false;
        hw75_diag_log_event(
            HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, touchbar.trace_id,
            HW75_DIAG_EVENT_TOUCHBAR_GESTURE_ACTIVE,
            hw75_diag_pack_u8x4(touchbar_diag_mode(touchbar.mode), touchbar.active_touch_count,
                                touch_state, 0U),
            true, (uint16_t)touchbar.current_position);
    }

    switch (touchbar.mode) {
    case TOUCHBAR_MODE_PAN:
        touchbar_handle_pan_mode(now_ms);
        break;
    case TOUCHBAR_MODE_APP_SWITCH:
        touchbar_handle_app_switch_mode(now_ms, timestamp);
        break;
    case TOUCHBAR_MODE_DESKTOP_SWITCH:
        touchbar_handle_desktop_switch_mode(now_ms, timestamp);
        break;
    default:
        break;
    }

    touchbar_apply_holds(timestamp);
    touchbar_tick_pulses(timestamp);
    touchbar_update_snapshot(touch_state, HW75_DIAG_TOUCHBAR_PHASE_ACTIVE);
}

static void touchbar_poll_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t touch_state = 0U;
    int64_t raw_timestamp = 0;
    int err = hw75_kscan_74hc165_get_touchbar_state(touchbar_kscan_dev, &touch_state, &raw_timestamp);
    if (err == 0) {
        if (touchbar_last_poll_err != 0) {
            hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, 0U,
                                HW75_DIAG_EVENT_TOUCHBAR_POLL_RECOVERED,
                                (uint16_t)touchbar_last_poll_err, false, 0U);
            touchbar_last_poll_err = 0;
        }
        uint32_t now_ms = raw_timestamp > 0 ? (uint32_t)raw_timestamp : k_uptime_get_32();
        int64_t event_timestamp = raw_timestamp > 0 ? raw_timestamp : k_uptime_get();
        touchbar_process(touch_state, now_ms, event_timestamp);
    } else if (touchbar_last_poll_err != err) {
        touchbar_last_poll_err = err;
        hw75_diag_log_event(HW75_DIAG_LEVEL_ERROR, HW75_DIAG_MODULE_TOUCHBAR, 0U,
                            HW75_DIAG_EVENT_TOUCHBAR_POLL_ERROR, (uint16_t)err, false, 0U);
    }

    k_work_reschedule(&touchbar_poll_work, K_MSEC(touchbar_poll_interval_ms()));
}

static uint8_t touchbar_mask_from_positions(const uint8_t *positions, uint8_t count) {
    uint8_t mask = 0U;

    for (uint8_t index = 0; index < count; index++) {
        mask |= touchbar_logical_bit(positions[index]);
    }

    return mask;
}

static int touchbar_apply_mode(enum hw75_touchbar_mode next_mode, bool preview_indicator) {
    if ((uint32_t)next_mode >= TOUCHBAR_MODE_COUNT) {
        return -EINVAL;
    }

    int64_t timestamp = k_uptime_get();
    enum hw75_touchbar_mode previous_mode = touchbar.mode;
    uint32_t trace_id = hw75_diag_next_trace_id();

    touchbar.mode = next_mode;
    touchbar_clear_actions(timestamp);
    touchbar_cancel_synthetic(timestamp);
    LOG_INF("TouchBar mode -> %d", touchbar.mode);
    hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, trace_id,
                        HW75_DIAG_EVENT_TOUCHBAR_MODE_CYCLE,
                        hw75_diag_pack_u8x4(touchbar_diag_mode(previous_mode),
                                            touchbar_diag_mode(touchbar.mode), 0U, 0U),
                        false, 0U);
    touchbar_update_snapshot(0U, HW75_DIAG_TOUCHBAR_PHASE_MODE_CYCLE);

    if (preview_indicator) {
        touchbar_preview_mode_indicator(next_mode);
    }

    return 0;
}

enum hw75_touchbar_mode touchbar_get_mode(void) { return touchbar.mode; }

int touchbar_set_mode(enum hw75_touchbar_mode mode) { return touchbar_apply_mode(mode, true); }

int touchbar_cycle_mode(void) {
    return touchbar_apply_mode((enum hw75_touchbar_mode)((touchbar.mode + 1) % TOUCHBAR_MODE_COUNT),
                               true);
}

static uint8_t touchbar_positions_from_mask(uint8_t mask, uint8_t *positions, uint8_t capacity) {
    uint8_t count = 0U;

    for (uint8_t logical_position = 0U; logical_position < HW75_TOUCHBAR_CHANNEL_COUNT;
         logical_position++) {
        if ((mask & touchbar_logical_bit(logical_position)) == 0U) {
            continue;
        }

        if (count >= capacity) {
            return 0U;
        }

        positions[count++] = logical_position;
    }

    return count;
}

static int touchbar_apply_segment_masks(const uint8_t *segment_touch_masks,
                                        const uint8_t *segment_entry_masks,
                                        uint8_t segment_count) {
    if (segment_touch_masks == NULL || segment_entry_masks == NULL || segment_count == 0U ||
        segment_count > TOUCHBAR_SEGMENT_CAPACITY) {
        return -EINVAL;
    }

    uint8_t next_touch_map[TOUCHBAR_SEGMENT_CAPACITY][TOUCHBAR_TOUCH_MAP_CAPACITY] = {0};
    uint8_t next_touch_count[TOUCHBAR_SEGMENT_CAPACITY] = {0};
    uint8_t next_entry_map[TOUCHBAR_SEGMENT_CAPACITY][TOUCHBAR_ENTRY_MAP_CAPACITY] = {0};
    uint8_t next_entry_count[TOUCHBAR_SEGMENT_CAPACITY] = {0};

    for (uint8_t segment_index = 0U; segment_index < segment_count; segment_index++) {
        uint8_t touch_mask = segment_touch_masks[segment_index];
        uint8_t entry_mask = segment_entry_masks[segment_index];

        if ((entry_mask & ~touch_mask) != 0U) {
            return -EINVAL;
        }

        next_touch_count[segment_index] = touchbar_positions_from_mask(
            touch_mask, next_touch_map[segment_index], TOUCHBAR_TOUCH_MAP_CAPACITY);
        next_entry_count[segment_index] = touchbar_positions_from_mask(
            entry_mask, next_entry_map[segment_index], TOUCHBAR_ENTRY_MAP_CAPACITY);

        if (next_touch_count[segment_index] == 0U || next_entry_count[segment_index] == 0U) {
            return -EINVAL;
        }
    }

    memcpy(touchbar_segment_touch_map, next_touch_map, sizeof(next_touch_map));
    memcpy(touchbar_segment_touch_count, next_touch_count, sizeof(next_touch_count));
    memcpy(touchbar_segment_entry_touch_map, next_entry_map, sizeof(next_entry_map));
    memcpy(touchbar_segment_entry_touch_count, next_entry_count, sizeof(next_entry_count));
    touchbar_segment_count = segment_count;

    return 0;
}

static uint16_t touchbar_sanitize_nonzero_u16(uint16_t value, uint16_t fallback) {
    return value == 0U ? fallback : value;
}

static void touchbar_sanitize_runtime_config(void) {
    touchbar_pan_config.activation_ms =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.activation_ms, TOUCHBAR_ACTIVATION_MS);
    touchbar_pan_config.release_grace_ms =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.release_grace_ms, TOUCHBAR_RELEASE_GRACE_MS);
    touchbar_pan_config.poll_interval_ms =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.poll_interval_ms, TOUCHBAR_POLL_INTERVAL_MS);
    touchbar_pan_config.interval_ms =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.interval_ms, TOUCHBAR_PAN_INTERVAL_MS);
    touchbar_pan_config.deadzone =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.deadzone, TOUCHBAR_PAN_DEADZONE);
    touchbar_pan_config.position_scale =
        touchbar_sanitize_nonzero_u16(touchbar_pan_config.position_scale, TOUCHBAR_POSITION_SCALE);

    touchbar_app_switch_config.activation_ms = touchbar_sanitize_nonzero_u16(
        touchbar_app_switch_config.activation_ms, TOUCHBAR_APP_ACTIVATION_MS);
    touchbar_app_switch_config.release_grace_ms = touchbar_sanitize_nonzero_u16(
        touchbar_app_switch_config.release_grace_ms, TOUCHBAR_SWITCH_RELEASE_GRACE_MS);
    touchbar_app_switch_config.release_settle_ms = touchbar_sanitize_nonzero_u16(
        touchbar_app_switch_config.release_settle_ms, TOUCHBAR_APP_RELEASE_SETTLE_MS);
    touchbar_app_switch_config.step_interval_ms = touchbar_sanitize_nonzero_u16(
        touchbar_app_switch_config.step_interval_ms, TOUCHBAR_APP_STEP_INTERVAL_MS);
    touchbar_app_switch_config.step_distance =
        touchbar_sanitize_nonzero_u16(touchbar_app_switch_config.step_distance, TOUCHBAR_STEP_DISTANCE);
    touchbar_app_switch_config.edge_repeat_delay_ms = touchbar_sanitize_nonzero_u16(
        touchbar_app_switch_config.edge_repeat_delay_ms, TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS);

    touchbar_desktop_switch_config.activation_ms = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.activation_ms, TOUCHBAR_ACTIVATION_MS);
    touchbar_desktop_switch_config.release_grace_ms = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.release_grace_ms, TOUCHBAR_SWITCH_RELEASE_GRACE_MS);
    touchbar_desktop_switch_config.hold_ms =
        touchbar_sanitize_nonzero_u16(touchbar_desktop_switch_config.hold_ms, TOUCHBAR_DESKTOP_HOLD_MS);
    touchbar_desktop_switch_config.step_interval_ms = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.step_interval_ms, TOUCHBAR_DESKTOP_STEP_INTERVAL_MS);
    touchbar_desktop_switch_config.step_distance = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.step_distance, TOUCHBAR_DESKTOP_STEP_DISTANCE);
    touchbar_desktop_switch_config.edge_repeat_delay_ms = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.edge_repeat_delay_ms, TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS);
    touchbar_desktop_switch_config.swipe_distance = touchbar_sanitize_nonzero_u16(
        touchbar_desktop_switch_config.swipe_distance, TOUCHBAR_DESKTOP_SWIPE_DISTANCE);

    for (size_t index = 0; index < ARRAY_SIZE(touchbar_mode_indicators); index++) {
        touchbar_mode_indicators[index].duration_ms = touchbar_sanitize_nonzero_u16(
            touchbar_mode_indicators[index].duration_ms, TOUCHBAR_MODE_INDICATOR_DURATION_MS);
    }
}

int touchbar_get_config_view(struct hw75_touchbar_config_view *view) {
    if (view == NULL) {
        return -EINVAL;
    }

    memset(view, 0, sizeof(*view));
    view->mode = touchbar.mode;
    view->logical_point_count = HW75_TOUCHBAR_CHANNEL_COUNT;
    view->segment_count = touchbar_segment_count;
    view->shared_point_count = touchbar_shared_point_count();
    for (uint8_t segment_index = 0U; segment_index < touchbar_segment_count; segment_index++) {
        view->segment_touch_masks[segment_index] = touchbar_mask_from_positions(
            touchbar_segment_touch_map[segment_index], touchbar_segment_touch_count[segment_index]);
        view->segment_entry_masks[segment_index] = touchbar_mask_from_positions(
            touchbar_segment_entry_touch_map[segment_index],
            touchbar_segment_entry_touch_count[segment_index]);
    }
    hw75_kscan_74hc165_get_touchbar_logical_map(view->logical_rows, view->logical_cols);
    view->mode_indicator_enabled = touchbar_mode_indicator_enabled;
    memcpy(view->mode_indicators, touchbar_mode_indicators, sizeof(touchbar_mode_indicators));
    view->pan = touchbar_pan_config;
    view->app_switch = touchbar_app_switch_config;
    view->desktop_switch = touchbar_desktop_switch_config;

    return 0;
}

int touchbar_set_config_view(const struct hw75_touchbar_config_view *view) {
    if (view == NULL || (uint32_t)view->mode >= TOUCHBAR_MODE_COUNT || view->segment_count == 0U ||
        view->segment_count > TOUCHBAR_SEGMENT_CAPACITY) {
        return -EINVAL;
    }

    if (touchbar_apply_segment_masks(view->segment_touch_masks, view->segment_entry_masks,
                                     view->segment_count) != 0) {
        return -EINVAL;
    }

    touchbar_pan_config = view->pan;
    touchbar_app_switch_config = view->app_switch;
    touchbar_desktop_switch_config = view->desktop_switch;
    touchbar_mode_indicator_enabled = view->mode_indicator_enabled;
    memcpy(touchbar_mode_indicators, view->mode_indicators, sizeof(touchbar_mode_indicators));
    touchbar_sanitize_runtime_config();

    if (touchbar.mode != view->mode) {
        return touchbar_apply_mode(view->mode, touchbar_mode_indicator_enabled);
    }

    int64_t timestamp = k_uptime_get();
    touchbar_clear_actions(timestamp);
    touchbar_cancel_synthetic(timestamp);
    touchbar_update_snapshot(0U, HW75_DIAG_TOUCHBAR_PHASE_MODE_CYCLE);
    k_work_reschedule(&touchbar_poll_work, K_MSEC(touchbar_poll_interval_ms()));
    return 0;
}

static int touchbar_init(const struct device *dev) {
    ARG_UNUSED(dev);

    touchbar_kscan_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_kscan));
    if (!device_is_ready(touchbar_kscan_dev)) {
        hw75_diag_log_event(HW75_DIAG_LEVEL_ERROR, HW75_DIAG_MODULE_TOUCHBAR, 0U,
                            HW75_DIAG_EVENT_TOUCHBAR_INIT_FAIL, 0U, false, 0U);
        return -ENODEV;
    }

    k_work_init_delayable(&touchbar_poll_work, touchbar_poll_handler);
    touchbar_sanitize_runtime_config();
    k_work_reschedule(&touchbar_poll_work, K_NO_WAIT);
    hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_TOUCHBAR, 0U,
                        HW75_DIAG_EVENT_TOUCHBAR_INIT, touchbar_poll_interval_ms(), false, 0U);
    touchbar_update_snapshot(0U, HW75_DIAG_TOUCHBAR_PHASE_INIT);

    return 0;
}

SYS_INIT(touchbar_init, APPLICATION, TOUCHBAR_INIT_PRIORITY);

#else

int touchbar_cycle_mode(void) { return -ENOTSUP; }
int touchbar_set_mode(enum hw75_touchbar_mode mode) {
    ARG_UNUSED(mode);
    return -ENOTSUP;
}
enum hw75_touchbar_mode touchbar_get_mode(void) { return HW75_TOUCHBAR_MODE_PAN; }
int touchbar_get_config_view(struct hw75_touchbar_config_view *view) {
    ARG_UNUSED(view);
    return -ENOTSUP;
}

#endif
