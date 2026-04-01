/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>

#include <app/hid_mouse.h>

#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include "touchbar_app.h"

#define TOUCHBAR_FIRST_POSITION 82
#define TOUCHBAR_POSITION_COUNT 6
#define TOUCHBAR_SEGMENT_COUNT 2
#define TOUCHBAR_TOUCHES_PER_SEGMENT 4
#define TOUCHBAR_ENTRY_TOUCHES_PER_SEGMENT 3
#define TOUCHBAR_INVALID_SEGMENT 0xFF

#define TOUCHBAR_ACTIVATION_MS 20
#define TOUCHBAR_APP_ACTIVATION_MS 90
#define TOUCHBAR_DESKTOP_HOLD_MS 500
#define TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS 400
#define TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS 1200
#define TOUCHBAR_APP_RELEASE_SETTLE_MS 50
#define TOUCHBAR_RELEASE_GRACE_MS 35
#define TOUCHBAR_SWITCH_RELEASE_GRACE_MS 90
#define TOUCHBAR_PAN_INTERVAL_MS 12
#define TOUCHBAR_APP_STEP_INTERVAL_MS 55
#define TOUCHBAR_DESKTOP_STEP_INTERVAL_MS 500
#define TOUCHBAR_POSITION_SCALE 256
#define TOUCHBAR_DESKTOP_SWIPE_DISTANCE 96
#define TOUCHBAR_EDGE_REPEAT_THRESHOLD 64
#define TOUCHBAR_PAN_DEADZONE 64
#define TOUCHBAR_STEP_DISTANCE 160
#define TOUCHBAR_DESKTOP_STEP_DISTANCE 256
#define TOUCHBAR_PROCESS_INTERVAL_MS 10

#define KEYMAP_NODE DT_INST(0, zmk_keymap)
#define KEYMAP_LAYER_CHILD_LEN(node) 1 +
#define KEYMAP_LAYERS_NUM (DT_FOREACH_CHILD(KEYMAP_NODE, KEYMAP_LAYER_CHILD_LEN) 0)

#define LAYER_LABEL(node) COND_CODE_0(DT_NODE_HAS_PROP(node, label), (NULL), (DT_PROP(node, label))),
#define LAYER_NAME(node) COND_CODE_0(DT_NODE_HAS_PROP(node, label), (DT_NODE_FULL_NAME(node)), (DT_PROP(node, label)))
#define LAYER_TOUCHBAR_PREF(node)                                                                  \
	{                                                                                          \
		.active = false,                                                                   \
		.name = LAYER_NAME(node),                                                          \
		.mode = TOUCHBAR_MODE_PAN,                                                         \
	},

struct touchbar_session {
	bool is_touching;
	bool is_gesture_active;
	bool is_desktop_seek_mode;
	bool is_no_touch_pending;
	bool shift_scroll_primed;
	uint8_t active_segment;
	uint8_t active_touch_count;
	uint32_t touch_start_ms;
	uint32_t last_touch_ms;
	uint32_t last_pan_ms;
	uint32_t last_step_ms;
	uint32_t edge_hold_start_ms;
	uint32_t app_switch_release_guard_until_ms;
	int16_t anchor_position;
	int16_t current_position;
	int16_t emitted_steps;
	int8_t edge_hold_direction;
};

struct touchbar_settings {
	bool enable;
	enum touchbar_mode default_mode;
};

static const char *layer_names[KEYMAP_LAYERS_NUM] = { DT_FOREACH_CHILD(KEYMAP_NODE, LAYER_LABEL) };
static const struct touchbar_pref default_prefs[KEYMAP_LAYERS_NUM] = { DT_FOREACH_CHILD(
	KEYMAP_NODE, LAYER_TOUCHBAR_PREF) };

static struct touchbar_settings settings = {
	.enable = true,
	.default_mode = TOUCHBAR_MODE_PAN,
};

static struct touchbar_pref touchbar_prefs[KEYMAP_LAYERS_NUM];
static struct touchbar_session session;
static struct k_work_delayable process_work;
static struct k_work_delayable save_work;
static struct k_mutex lock;

static bool touch_states[TOUCHBAR_POSITION_COUNT];
static enum touchbar_mode current_mode = TOUCHBAR_MODE_PAN;
static bool pan_shift_held;
static bool app_alt_held;

static const uint8_t touchbar_segment_touch_map[TOUCHBAR_SEGMENT_COUNT][TOUCHBAR_TOUCHES_PER_SEGMENT] = {
	{0, 1, 2, 3},
	{2, 3, 4, 5},
};

static const uint8_t
	touchbar_segment_entry_touch_map[TOUCHBAR_SEGMENT_COUNT][TOUCHBAR_ENTRY_TOUCHES_PER_SEGMENT] = {
		{0, 1, 2},
		{3, 4, 5},
	};

static int touchbar_send_keyboard_report(void)
{
	return zmk_endpoints_send_report(HID_USAGE_KEY);
}

static bool touchbar_has_physical_touch(void)
{
	for (int i = 0; i < TOUCHBAR_POSITION_COUNT; i++) {
		if (touch_states[i]) {
			return true;
		}
	}
	return false;
}

static void touchbar_release_pan_shift(void)
{
	if (!pan_shift_held) {
		return;
	}

	zmk_hid_unregister_mods(MOD_LSFT);
	touchbar_send_keyboard_report();
	pan_shift_held = false;
}

static void touchbar_release_app_alt(void)
{
	if (!app_alt_held) {
		return;
	}

	zmk_hid_unregister_mods(MOD_LALT);
	touchbar_send_keyboard_report();
	app_alt_held = false;
}

static void touchbar_hold_pan_shift(void)
{
	if (pan_shift_held) {
		return;
	}

	zmk_hid_register_mods(MOD_LSFT);
	touchbar_send_keyboard_report();
	pan_shift_held = true;
}

static void touchbar_hold_app_alt(void)
{
	if (app_alt_held) {
		return;
	}

	zmk_hid_register_mods(MOD_LALT);
	touchbar_send_keyboard_report();
	app_alt_held = true;
}

static void touchbar_tap_usage(uint32_t usage)
{
	zmk_hid_press(usage);
	touchbar_send_keyboard_report();
	zmk_hid_release(usage);
	touchbar_send_keyboard_report();
}

static void touchbar_emit_mouse_wheel(int8_t wheel)
{
	if (wheel == 0) {
		return;
	}

	hid_mouse_wheel_report(wheel, true);
	hid_mouse_wheel_report(wheel, false);
}

static inline int16_t abs16(int16_t value)
{
	return value >= 0 ? value : -value;
}

static uint8_t count_mapped_touches(const uint8_t *logical_positions, uint8_t logical_count)
{
	uint8_t touch_count = 0;

	for (uint8_t i = 0; i < logical_count; i++) {
		if (touch_states[logical_positions[i]]) {
			touch_count++;
		}
	}

	return touch_count;
}

static int16_t get_mapped_position(const uint8_t *logical_positions, uint8_t logical_count)
{
	uint16_t weighted_sum = 0;
	uint8_t active_count = 0;

	for (uint8_t i = 0; i < logical_count; i++) {
		if (touch_states[logical_positions[i]]) {
			weighted_sum += i * TOUCHBAR_POSITION_SCALE;
			active_count++;
		}
	}

	if (active_count == 0) {
		return -1;
	}

	return weighted_sum / active_count;
}

static uint8_t count_touchbar_entry_touches(uint8_t segment_index)
{
	return count_mapped_touches(touchbar_segment_entry_touch_map[segment_index],
				    TOUCHBAR_ENTRY_TOUCHES_PER_SEGMENT);
}

static uint8_t count_touchbar_segment_touches(uint8_t segment_index)
{
	return count_mapped_touches(touchbar_segment_touch_map[segment_index],
				    TOUCHBAR_TOUCHES_PER_SEGMENT);
}

static int16_t get_touchbar_segment_position(uint8_t segment_index)
{
	return get_mapped_position(touchbar_segment_touch_map[segment_index],
				   TOUCHBAR_TOUCHES_PER_SEGMENT);
}

static int16_t get_touchbar_global_position(void)
{
	static const uint8_t touchbar_global_touch_map[TOUCHBAR_POSITION_COUNT] = {0, 1, 2, 3, 4, 5};

	return get_mapped_position(touchbar_global_touch_map, TOUCHBAR_POSITION_COUNT);
}

static uint8_t select_touchbar_segment(void)
{
	uint8_t left_entry_touches = count_touchbar_entry_touches(0);
	uint8_t right_entry_touches = count_touchbar_entry_touches(1);

	if (left_entry_touches == 0 && right_entry_touches == 0) {
		return TOUCHBAR_INVALID_SEGMENT;
	}
	if (left_entry_touches > right_entry_touches) {
		return 0;
	}
	if (right_entry_touches > left_entry_touches) {
		return 1;
	}

	return get_touchbar_global_position() < (3 * TOUCHBAR_POSITION_SCALE) ? 0
								       : 1;
}

static uint32_t get_touchbar_activation_delay_ms(void)
{
	return current_mode == TOUCHBAR_MODE_APP_SWITCH ? TOUCHBAR_APP_ACTIVATION_MS
						       : TOUCHBAR_ACTIVATION_MS;
}

static uint32_t get_touchbar_release_grace_ms(void)
{
	switch (current_mode) {
	case TOUCHBAR_MODE_APP_SWITCH:
	case TOUCHBAR_MODE_DESKTOP_SWITCH:
		return TOUCHBAR_SWITCH_RELEASE_GRACE_MS;
	default:
		return TOUCHBAR_RELEASE_GRACE_MS;
	}
}

static void clear_touchbar_actions(void)
{
	memset(&session, 0, sizeof(session));
	session.active_segment = TOUCHBAR_INVALID_SEGMENT;
	touchbar_release_pan_shift();
	touchbar_release_app_alt();
}

static int16_t get_touchbar_edge_direction(int16_t position)
{
	int16_t max_position = (TOUCHBAR_TOUCHES_PER_SEGMENT - 1) * TOUCHBAR_POSITION_SCALE;

	if (position <= TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
		return -1;
	}
	if (position >= max_position - TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
		return 1;
	}

	return 0;
}

static void reset_touchbar_edge_hold(void)
{
	session.edge_hold_start_ms = 0;
	session.edge_hold_direction = 0;
}

static void arm_touchbar_edge_hold(uint32_t now_ms, int16_t edge_direction)
{
	session.edge_hold_direction = edge_direction;
	session.edge_hold_start_ms = now_ms;
}

static void queue_app_switch_step(int16_t direction)
{
	touchbar_hold_app_alt();

	if (direction < 0) {
		zmk_hid_register_mods(MOD_LSFT);
		touchbar_send_keyboard_report();
	}

	touchbar_tap_usage(TAB);

	if (direction < 0) {
		zmk_hid_unregister_mods(MOD_LSFT);
		touchbar_send_keyboard_report();
	}
}

static void queue_desktop_switch_step(int16_t direction)
{
	zmk_hid_register_mods(MOD_LCTL | MOD_LGUI);
	touchbar_send_keyboard_report();
	touchbar_tap_usage(direction < 0 ? LEFT : RIGHT);
	zmk_hid_unregister_mods(MOD_LCTL | MOD_LGUI);
	touchbar_send_keyboard_report();
}

static bool should_delay_desktop_edge_continuation(uint32_t now_ms, int16_t target_steps)
{
	int16_t edge_direction = get_touchbar_edge_direction(session.current_position);
	int16_t pending_direction;

	if (edge_direction == 0) {
		reset_touchbar_edge_hold();
		return false;
	}

	pending_direction = target_steps > session.emitted_steps ? 1 : -1;
	if (pending_direction != edge_direction) {
		return false;
	}

	if (session.edge_hold_direction != edge_direction) {
		arm_touchbar_edge_hold(now_ms, edge_direction);
		return false;
	}

	return now_ms - session.edge_hold_start_ms < TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS;
}

static bool try_repeat_step_at_edge(uint32_t now_ms, uint32_t hold_delay_ms,
				    uint32_t step_interval_ms, int16_t step_distance,
				    void (*queue_step)(int16_t))
{
	int16_t edge_direction = get_touchbar_edge_direction(session.current_position);

	if (edge_direction == 0) {
		reset_touchbar_edge_hold();
		return false;
	}

	if (session.edge_hold_direction != edge_direction) {
		arm_touchbar_edge_hold(now_ms, edge_direction);
		return true;
	}
	if (now_ms - session.edge_hold_start_ms < hold_delay_ms) {
		return true;
	}
	if (now_ms - session.last_step_ms < step_interval_ms) {
		return true;
	}

	session.last_step_ms = now_ms;
	queue_step(edge_direction);
	session.emitted_steps += edge_direction;
	session.anchor_position -= edge_direction * step_distance;

	return true;
}

static void handle_pan_mode(uint32_t now_ms)
{
	int16_t displacement;
	int16_t distance;
	int16_t speed;

	if (now_ms - session.last_pan_ms < TOUCHBAR_PAN_INTERVAL_MS) {
		return;
	}

	session.last_pan_ms = now_ms;
	displacement = session.current_position - session.anchor_position;
	distance = abs16(displacement);
	if (distance <= TOUCHBAR_PAN_DEADZONE) {
		return;
	}

	speed = 1 + (distance - TOUCHBAR_PAN_DEADZONE) / (TOUCHBAR_POSITION_SCALE / 2);
	if (speed > 6) {
		speed = 6;
	}

	touchbar_hold_pan_shift();
	if (!session.shift_scroll_primed) {
		session.shift_scroll_primed = true;
		return;
	}

	touchbar_emit_mouse_wheel(displacement > 0 ? -speed : speed);
}

static void handle_app_switch_mode(uint32_t now_ms)
{
	int16_t displacement;
	int16_t target_steps;

	if (now_ms < session.app_switch_release_guard_until_ms) {
		return;
	}

	displacement = session.current_position - session.anchor_position;
	target_steps = displacement / TOUCHBAR_STEP_DISTANCE;

	if (target_steps == session.emitted_steps) {
		try_repeat_step_at_edge(now_ms, TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS,
					TOUCHBAR_APP_STEP_INTERVAL_MS, TOUCHBAR_STEP_DISTANCE,
					queue_app_switch_step);
		return;
	}
	if (now_ms - session.last_step_ms < TOUCHBAR_APP_STEP_INTERVAL_MS) {
		return;
	}

	session.last_step_ms = now_ms;
	reset_touchbar_edge_hold();

	if (target_steps > session.emitted_steps) {
		queue_app_switch_step(1);
		session.emitted_steps++;
	} else {
		queue_app_switch_step(-1);
		session.emitted_steps--;
	}
}

static void finalize_desktop_switch_gesture(void)
{
	int16_t displacement;

	if (!session.is_gesture_active || session.is_desktop_seek_mode) {
		return;
	}

	displacement = session.current_position - session.anchor_position;
	if (abs16(displacement) < TOUCHBAR_DESKTOP_SWIPE_DISTANCE) {
		return;
	}

	queue_desktop_switch_step(displacement > 0 ? 1 : -1);
}

static void handle_desktop_switch_mode(uint32_t now_ms)
{
	int16_t displacement;
	int16_t target_steps;

	if (!session.is_desktop_seek_mode) {
		if (now_ms - session.touch_start_ms < TOUCHBAR_DESKTOP_HOLD_MS) {
			return;
		}

		session.is_desktop_seek_mode = true;
		session.anchor_position = session.current_position;
		session.emitted_steps = 0;
		reset_touchbar_edge_hold();
		session.last_step_ms = now_ms - TOUCHBAR_DESKTOP_STEP_INTERVAL_MS;
		return;
	}

	displacement = session.current_position - session.anchor_position;
	target_steps = displacement / TOUCHBAR_DESKTOP_STEP_DISTANCE;

	if (target_steps == session.emitted_steps) {
		try_repeat_step_at_edge(now_ms, TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS,
					TOUCHBAR_DESKTOP_STEP_INTERVAL_MS,
					TOUCHBAR_DESKTOP_STEP_DISTANCE,
					queue_desktop_switch_step);
		return;
	}
	if (should_delay_desktop_edge_continuation(now_ms, target_steps)) {
		return;
	}
	if (now_ms - session.last_step_ms < TOUCHBAR_DESKTOP_STEP_INTERVAL_MS) {
		return;
	}

	session.last_step_ms = now_ms;
	if (get_touchbar_edge_direction(session.current_position) == 0) {
		reset_touchbar_edge_hold();
	}

	if (target_steps > session.emitted_steps) {
		queue_desktop_switch_step(1);
		session.emitted_steps++;
	} else {
		queue_desktop_switch_step(-1);
		session.emitted_steps--;
	}
}

static void process_touchbar(uint32_t now_ms)
{
	uint8_t active_segment = TOUCHBAR_INVALID_SEGMENT;
	uint8_t active_touch_count = 0;
	int16_t touch_position = -1;

	if (!settings.enable || current_mode == TOUCHBAR_MODE_DISABLE) {
		clear_touchbar_actions();
		return;
	}

	if (session.is_touching) {
		active_segment = session.active_segment;
		if (active_segment < TOUCHBAR_SEGMENT_COUNT) {
			touch_position = get_touchbar_segment_position(active_segment);
			active_touch_count = count_touchbar_segment_touches(active_segment);
		}
	} else {
		active_segment = select_touchbar_segment();
		if (active_segment < TOUCHBAR_SEGMENT_COUNT) {
			touch_position = get_touchbar_segment_position(active_segment);
			active_touch_count = count_touchbar_segment_touches(active_segment);
		}
	}

	if (touch_position < 0) {
		if (!session.is_touching) {
			return;
		}

		if (!session.is_no_touch_pending) {
			session.is_no_touch_pending = true;
			session.last_touch_ms = now_ms;
			return;
		}

		if (now_ms - session.last_touch_ms < get_touchbar_release_grace_ms()) {
			return;
		}

		if (current_mode == TOUCHBAR_MODE_DESKTOP_SWITCH) {
			finalize_desktop_switch_gesture();
		}
		clear_touchbar_actions();
		return;
	}

	session.is_no_touch_pending = false;
	session.last_touch_ms = now_ms;
	session.current_position = touch_position;

	if (current_mode == TOUCHBAR_MODE_APP_SWITCH) {
		bool release_order_jitter = session.is_gesture_active &&
					    session.active_touch_count > active_touch_count &&
					    active_touch_count == 1;

		if (release_order_jitter) {
			session.app_switch_release_guard_until_ms =
				now_ms + TOUCHBAR_APP_RELEASE_SETTLE_MS;
		} else if (active_touch_count > 1) {
			session.app_switch_release_guard_until_ms = 0;
		}
	}

	if (!session.is_touching) {
		memset(&session, 0, sizeof(session));
		session.is_touching = true;
		session.active_segment = active_segment;
		session.active_touch_count = active_touch_count;
		session.touch_start_ms = now_ms;
		session.last_touch_ms = now_ms;
		session.anchor_position = touch_position;
		session.current_position = touch_position;
		session.last_pan_ms = now_ms;
		session.last_step_ms = now_ms;
		session.active_segment = active_segment;
		session.shift_scroll_primed = false;
		return;
	}

	session.active_touch_count = active_touch_count;

	if (!session.is_gesture_active) {
		if (now_ms - session.touch_start_ms < get_touchbar_activation_delay_ms()) {
			return;
		}

		session.is_gesture_active = true;
	}

	switch (current_mode) {
	case TOUCHBAR_MODE_PAN:
		handle_pan_mode(now_ms);
		break;
	case TOUCHBAR_MODE_APP_SWITCH:
		handle_app_switch_mode(now_ms);
		break;
	case TOUCHBAR_MODE_DESKTOP_SWITCH:
		handle_desktop_switch_mode(now_ms);
		break;
	default:
		break;
	}
}

static enum touchbar_mode resolve_mode_for_layer(uint8_t layer_id)
{
	if (!settings.enable) {
		return TOUCHBAR_MODE_DISABLE;
	}

	if (layer_id < KEYMAP_LAYERS_NUM && touchbar_prefs[layer_id].active) {
		return touchbar_prefs[layer_id].mode;
	}

	return settings.default_mode;
}

static void apply_active_mode(void)
{
	enum touchbar_mode next_mode = resolve_mode_for_layer(zmk_keymap_highest_layer_active());

	if (current_mode == next_mode) {
		return;
	}

	clear_touchbar_actions();
	current_mode = next_mode;
}

static void touchbar_process_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&lock, K_FOREVER);
	process_touchbar(k_uptime_get_32());
	if (((settings.enable && current_mode != TOUCHBAR_MODE_DISABLE) &&
	     (touchbar_has_physical_touch() || session.is_touching)) ||
	    pan_shift_held || app_alt_held) {
		k_work_reschedule(&process_work, K_MSEC(TOUCHBAR_PROCESS_INTERVAL_MS));
	}
	k_mutex_unlock(&lock);
}

static void touchbar_save_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	settings_save_one("app/touchbar/settings", &settings, sizeof(settings));
	settings_save_one("app/touchbar/prefs", &touchbar_prefs, sizeof(touchbar_prefs));
}

static int touchbar_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
				     void *cb_arg, void *param)
{
	const char *next;
	int ret;

	ARG_UNUSED(param);

	if (settings_name_steq(name, "settings", &next) && !next) {
		if (len != sizeof(settings)) {
			return -EINVAL;
		}

		ret = read_cb(cb_arg, &settings, sizeof(settings));
		return ret >= 0 ? 0 : ret;
	}

	if (settings_name_steq(name, "prefs", &next) && !next) {
		struct touchbar_pref loader[KEYMAP_LAYERS_NUM];

		if (len != sizeof(loader)) {
			return -EINVAL;
		}

		ret = read_cb(cb_arg, &loader, sizeof(loader));
		if (ret < 0) {
			return ret;
		}

		for (uint8_t i = 0; i < ARRAY_SIZE(loader); i++) {
			if (!loader[i].active) {
				continue;
			}

			for (uint8_t j = 0; j < ARRAY_SIZE(touchbar_prefs); j++) {
				if (strcmp(loader[i].name, touchbar_prefs[j].name) == 0) {
					memcpy(&touchbar_prefs[j], &loader[i], sizeof(struct touchbar_pref));
					break;
				}
			}
		}

		return 0;
	}

	return -ENOENT;
}

static int touchbar_event_listener(const zmk_event_t *eh)
{
	struct zmk_position_state_changed *position_ev;

	if (as_zmk_layer_state_changed(eh)) {
		k_mutex_lock(&lock, K_FOREVER);
		apply_active_mode();
		k_mutex_unlock(&lock);
		return 0;
	}

	position_ev = as_zmk_position_state_changed(eh);
	if (position_ev == NULL) {
		return -ENOTSUP;
	}
	if (position_ev->position < TOUCHBAR_FIRST_POSITION ||
	    position_ev->position >= TOUCHBAR_FIRST_POSITION + TOUCHBAR_POSITION_COUNT) {
		return -ENOTSUP;
	}

	k_mutex_lock(&lock, K_FOREVER);
	touch_states[position_ev->position - TOUCHBAR_FIRST_POSITION] = position_ev->state;
	k_work_reschedule(&process_work, K_NO_WAIT);
	k_mutex_unlock(&lock);

	return 0;
}

bool touchbar_app_get_enable(void)
{
	return settings.enable;
}

void touchbar_app_set_enable(bool enable)
{
	k_mutex_lock(&lock, K_FOREVER);
	settings.enable = enable;
	apply_active_mode();
	k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
	k_mutex_unlock(&lock);
}

enum touchbar_mode touchbar_app_get_default_mode(void)
{
	return settings.default_mode;
}

void touchbar_app_set_default_mode(enum touchbar_mode mode)
{
	k_mutex_lock(&lock, K_FOREVER);
	settings.default_mode = mode;
	apply_active_mode();
	k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
	k_mutex_unlock(&lock);
}

enum touchbar_mode touchbar_app_get_mode(void)
{
	return current_mode;
}

int touchbar_app_get_prefs(const struct touchbar_pref **prefs, const char ***names)
{
	*prefs = &touchbar_prefs[0];
	if (names != NULL) {
		*names = &layer_names[0];
	}
	return KEYMAP_LAYERS_NUM;
}

const struct touchbar_pref *touchbar_app_get_pref(uint8_t layer_id)
{
	if (layer_id >= KEYMAP_LAYERS_NUM) {
		return NULL;
	}

	return &touchbar_prefs[layer_id];
}

void touchbar_app_set_pref(uint8_t layer_id, const struct touchbar_pref *pref)
{
	if (layer_id >= KEYMAP_LAYERS_NUM) {
		return;
	}

	k_mutex_lock(&lock, K_FOREVER);
	memcpy(&touchbar_prefs[layer_id], pref, sizeof(struct touchbar_pref));
	apply_active_mode();
	k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
	k_mutex_unlock(&lock);
}

void touchbar_app_reset_pref(uint8_t layer_id)
{
	if (layer_id >= KEYMAP_LAYERS_NUM) {
		return;
	}

	k_mutex_lock(&lock, K_FOREVER);
	memcpy(&touchbar_prefs[layer_id], &default_prefs[layer_id], sizeof(struct touchbar_pref));
	apply_active_mode();
	k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
	k_mutex_unlock(&lock);
}

static int touchbar_app_init(const struct device *dev)
{
	int ret;

	ARG_UNUSED(dev);

	memcpy(&touchbar_prefs, &default_prefs, sizeof(default_prefs));
	k_mutex_init(&lock);
	k_work_init_delayable(&process_work, touchbar_process_work_handler);
	k_work_init_delayable(&save_work, touchbar_save_work_handler);

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("Failed to initializing settings subsys: %d", ret);
	}

	ret = settings_load_subtree_direct("app/touchbar", touchbar_settings_load_cb, NULL);
	if (ret) {
		LOG_ERR("Failed to load touchbar settings: %d", ret);
	}

	current_mode = resolve_mode_for_layer(zmk_keymap_highest_layer_active());
	clear_touchbar_actions();

	return 0;
}

ZMK_LISTENER(touchbar_app, touchbar_event_listener);
ZMK_SUBSCRIPTION(touchbar_app, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(touchbar_app, zmk_position_state_changed);

SYS_INIT(touchbar_app_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
