/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * HelloWord TouchBar logic: pan uses Shift + vertical wheel so Windows treats it as
 * horizontal scroll (no dedicated HW wheel axis in our HID report). Sticky Keys may
 * prompt if enabled in OS. Alt+Tab switcher, virtual desktop.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/keys.h>

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include <app/hid_mouse.h>

#define TOUCHPAD_NUMBER 6
#define TOUCH_POS_0     82
#define POS_RCTRL       78
#define LAYER_FN        1

enum touchbar_mode {
	TOUCHBAR_MODE_PAN = 0,
	TOUCHBAR_MODE_APP_SWITCH,
	TOUCHBAR_MODE_DESKTOP_SWITCH,
	TOUCHBAR_MODE_COUNT,
};

struct touchbar_session {
	enum touchbar_mode mode;
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
	int16_t current_position;
	int16_t emitted_steps;
	int8_t edge_hold_direction;
};

static struct touchbar_session tb;

static const uint8_t TOUCHBAR_SEGMENT_COUNT = 2;
static const uint8_t TOUCHBAR_TOUCHES_PER_SEGMENT = 4;
static const uint8_t TOUCHBAR_ENTRY_TOUCHES_PER_SEGMENT = 3;
static const uint8_t TOUCHBAR_INVALID_SEGMENT = 0xFF;
static const uint32_t TOUCHBAR_ACTIVATION_MS = 20;
static const uint32_t TOUCHBAR_APP_ACTIVATION_MS = 105;
static const uint32_t TOUCHBAR_DESKTOP_HOLD_MS = 320;
static const uint32_t TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS = 520;
static const uint32_t TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS = 750;
static const uint32_t TOUCHBAR_APP_RELEASE_SETTLE_MS = 50;
static const uint32_t TOUCHBAR_RELEASE_GRACE_MS = 35;
static const uint32_t TOUCHBAR_SWITCH_RELEASE_GRACE_MS = 90;
static const uint32_t TOUCHBAR_PAN_INTERVAL_MS = 12;
static const uint32_t TOUCHBAR_APP_STEP_INTERVAL_MS = 85;
static const uint32_t TOUCHBAR_DESKTOP_STEP_INTERVAL_MS = 380;
static const int16_t TOUCHBAR_POSITION_SCALE = 256;
static const int16_t TOUCHBAR_DESKTOP_SWIPE_DISTANCE = 72;
static const int16_t TOUCHBAR_EDGE_REPEAT_THRESHOLD = 64;
static const int16_t TOUCHBAR_PAN_DEADZONE = 48;
static const int16_t TOUCHBAR_STEP_DISTANCE = 280;
static const int16_t TOUCHBAR_DESKTOP_STEP_DISTANCE = 192;

static const uint8_t TOUCHBAR_SEGMENT_TOUCH_MAP[2][4] = {{0, 1, 2, 3}, {2, 3, 4, 5}};
static const uint8_t TOUCHBAR_SEGMENT_ENTRY_TOUCH_MAP[2][3] = {{0, 1, 2}, {3, 4, 5}};

static bool pos_down[88];
static bool prev_rctrl;

static struct k_work_delayable touchbar_work;

static uint8_t get_touch_bar_logical_bit(uint8_t logical_position)
{
	return (uint8_t)(1U << (TOUCHPAD_NUMBER - 1U - logical_position));
}

static uint8_t count_mapped_touches(uint8_t touch_state, const uint8_t *logical_positions,
				    uint8_t logical_count)
{
	uint8_t n = 0;
	for (uint8_t i = 0; i < logical_count; i++) {
		if (touch_state & get_touch_bar_logical_bit(logical_positions[i])) {
			n++;
		}
	}
	return n;
}

static int16_t get_mapped_position(uint8_t touch_state, const uint8_t *logical_positions,
				   uint8_t logical_count)
{
	uint32_t sum = 0;
	uint8_t cnt = 0;
	for (uint8_t i = 0; i < logical_count; i++) {
		if (touch_state & get_touch_bar_logical_bit(logical_positions[i])) {
			sum += (uint32_t)i * TOUCHBAR_POSITION_SCALE;
			cnt++;
		}
	}
	if (cnt == 0) {
		return -1;
	}
	return (int16_t)(sum / cnt);
}

static uint8_t count_segment_touches(uint8_t touch_state, uint8_t seg)
{
	return count_mapped_touches(touch_state, TOUCHBAR_SEGMENT_TOUCH_MAP[seg],
				    TOUCHBAR_TOUCHES_PER_SEGMENT);
}

static uint8_t count_entry_touches(uint8_t touch_state, uint8_t seg)
{
	return count_mapped_touches(touch_state, TOUCHBAR_SEGMENT_ENTRY_TOUCH_MAP[seg],
				    TOUCHBAR_ENTRY_TOUCHES_PER_SEGMENT);
}

static int16_t get_segment_position(uint8_t touch_state, uint8_t seg)
{
	return get_mapped_position(touch_state, TOUCHBAR_SEGMENT_TOUCH_MAP[seg],
				   TOUCHBAR_TOUCHES_PER_SEGMENT);
}

static int16_t get_global_position(uint8_t touch_state)
{
	static const uint8_t gmap[6] = {0, 1, 2, 3, 4, 5};
	return get_mapped_position(touch_state, gmap, TOUCHPAD_NUMBER);
}

static uint8_t select_segment(uint8_t touch_state)
{
	uint8_t le = count_entry_touches(touch_state, 0);
	uint8_t re = count_entry_touches(touch_state, 1);
	if (le == 0 && re == 0) {
		return TOUCHBAR_INVALID_SEGMENT;
	}
	if (le > re) {
		return 0;
	}
	if (re > le) {
		return 1;
	}
	int16_t g = get_global_position(touch_state);
	if (g < 0) {
		return TOUCHBAR_INVALID_SEGMENT;
	}
	return g < (3 * TOUCHBAR_POSITION_SCALE) ? 0 : 1;
}

static uint8_t raw_touch_state(void)
{
	uint8_t raw = 0;
	for (uint8_t i = 0; i < TOUCHPAD_NUMBER; i++) {
		if (pos_down[TOUCH_POS_0 + i]) {
			raw |= (uint8_t)(1U << i);
		}
	}
	static const uint8_t RAW_BIT_BY_LOGICAL[6] = {0, 5, 4, 3, 2, 1};
	uint8_t logical = 0;
	for (uint8_t lp = 0; lp < TOUCHPAD_NUMBER; lp++) {
		uint8_t rb = RAW_BIT_BY_LOGICAL[lp];
		if (raw & (1U << rb)) {
			logical |= (uint8_t)(1U << (TOUCHPAD_NUMBER - 1U - lp));
		}
	}
	return logical;
}

static inline int16_t abs16(int16_t v)
{
	return v >= 0 ? v : (int16_t)-v;
}

static uint32_t activation_delay_ms(void)
{
	return tb.mode == TOUCHBAR_MODE_APP_SWITCH ? TOUCHBAR_APP_ACTIVATION_MS : TOUCHBAR_ACTIVATION_MS;
}

static uint32_t release_grace_ms(void)
{
	return (tb.mode == TOUCHBAR_MODE_APP_SWITCH || tb.mode == TOUCHBAR_MODE_DESKTOP_SWITCH)
		       ? TOUCHBAR_SWITCH_RELEASE_GRACE_MS
		       : TOUCHBAR_RELEASE_GRACE_MS;
}

static void clear_touchbar_actions(void)
{
	tb.is_touching = false;
	tb.is_gesture_active = false;
	tb.is_desktop_seek_mode = false;
	tb.is_no_touch_pending = false;
	tb.active_segment = TOUCHBAR_INVALID_SEGMENT;
	tb.active_touch_count = 0;
	tb.touch_start_ms = 0;
	tb.last_touch_ms = 0;
	tb.last_pan_ms = 0;
	tb.last_step_ms = 0;
	tb.edge_hold_start_ms = 0;
	tb.app_switch_release_guard_until_ms = 0;
	tb.anchor_position = 0;
	tb.current_position = 0;
	tb.emitted_steps = 0;
	tb.edge_hold_direction = 0;
}

static void cycle_touchbar_mode(void)
{
	tb.mode = (enum touchbar_mode)((tb.mode + 1) % TOUCHBAR_MODE_COUNT);
	LOG_INF("TouchBar mode %u", (unsigned)tb.mode);
}

static int send_kb_report(void)
{
	return zmk_endpoints_send_report(HID_USAGE_KEY);
}

static void queue_mouse_wheel(int8_t w)
{
	if (w == 0) {
		return;
	}
	/* Shift + vertical wheel -> horizontal scroll in most Windows apps */
	zmk_hid_implicit_modifiers_press(1 << 1);
	send_kb_report();
	hid_mouse_wheel_report(w, true);
	zmk_hid_implicit_modifiers_release();
	send_kb_report();
}

static void queue_app_switch_step(int16_t direction)
{
	zmk_hid_register_mod(2);
	send_kb_report();
	if (direction < 0) {
		zmk_hid_register_mod(1);
		send_kb_report();
	}
	zmk_hid_press(TAB);
	send_kb_report();
	zmk_hid_release(TAB);
	send_kb_report();
	if (direction < 0) {
		zmk_hid_unregister_mod(1);
		send_kb_report();
	}
	zmk_hid_unregister_mod(2);
	send_kb_report();
}

static void queue_desktop_switch_step(int16_t direction)
{
	zmk_hid_register_mod(0);
	zmk_hid_register_mod(3);
	send_kb_report();
	if (direction < 0) {
		zmk_hid_press(LEFT_ARROW);
	} else {
		zmk_hid_press(RIGHT_ARROW);
	}
	send_kb_report();
	if (direction < 0) {
		zmk_hid_release(LEFT_ARROW);
	} else {
		zmk_hid_release(RIGHT_ARROW);
	}
	send_kb_report();
	zmk_hid_unregister_mod(0);
	zmk_hid_unregister_mod(3);
	send_kb_report();
}

static void handle_pan_mode(uint32_t now_ms)
{
	if (now_ms - tb.last_pan_ms < TOUCHBAR_PAN_INTERVAL_MS) {
		return;
	}
	tb.last_pan_ms = now_ms;
	int16_t displacement = tb.current_position - tb.anchor_position;
	int16_t distance = abs16(displacement);
	if (distance <= TOUCHBAR_PAN_DEADZONE) {
		return;
	}
	int16_t speed = 1 + (distance - TOUCHBAR_PAN_DEADZONE) / (TOUCHBAR_POSITION_SCALE / 2);
	if (speed > 6) {
		speed = 6;
	}
	queue_mouse_wheel((int8_t)(displacement > 0 ? -speed : speed));
}

static int16_t edge_direction(int16_t position)
{
	int16_t maxp = (TOUCHBAR_TOUCHES_PER_SEGMENT - 1) * TOUCHBAR_POSITION_SCALE;
	if (position <= TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
		return -1;
	}
	if (position >= maxp - TOUCHBAR_EDGE_REPEAT_THRESHOLD) {
		return 1;
	}
	return 0;
}

static void reset_edge_hold(void)
{
	tb.edge_hold_start_ms = 0;
	tb.edge_hold_direction = 0;
}

static void arm_edge_hold(uint32_t now_ms, int16_t dir)
{
	tb.edge_hold_direction = (int8_t)dir;
	tb.edge_hold_start_ms = now_ms;
}

static bool try_repeat_at_edge(uint32_t now_ms, uint32_t hold_delay_ms, uint32_t step_interval_ms,
			       int16_t step_distance, void (*queue_step)(int16_t))
{
	int16_t ed = edge_direction(tb.current_position);
	if (ed == 0) {
		reset_edge_hold();
		return false;
	}
	if (tb.edge_hold_direction != ed) {
		arm_edge_hold(now_ms, ed);
		return true;
	}
	if (now_ms - tb.edge_hold_start_ms < hold_delay_ms) {
		return true;
	}
	if (now_ms - tb.last_step_ms < step_interval_ms) {
		return true;
	}
	tb.last_step_ms = now_ms;
	queue_step(ed);
	tb.emitted_steps = (int16_t)(tb.emitted_steps + ed);
	tb.anchor_position = (int16_t)(tb.anchor_position - ed * step_distance);
	return true;
}

static void handle_app_switch_mode(uint32_t now_ms)
{
	if (now_ms < tb.app_switch_release_guard_until_ms) {
		return;
	}
	int16_t displacement = tb.current_position - tb.anchor_position;
	int16_t target_steps = displacement / TOUCHBAR_STEP_DISTANCE;
	if (target_steps == tb.emitted_steps) {
		try_repeat_at_edge(now_ms, TOUCHBAR_APP_EDGE_REPEAT_DELAY_MS, TOUCHBAR_APP_STEP_INTERVAL_MS,
				   TOUCHBAR_STEP_DISTANCE, queue_app_switch_step);
		return;
	}
	if (now_ms - tb.last_step_ms < TOUCHBAR_APP_STEP_INTERVAL_MS) {
		return;
	}
	tb.last_step_ms = now_ms;
	tb.edge_hold_start_ms = 0;
	tb.edge_hold_direction = 0;
	if (target_steps > tb.emitted_steps) {
		queue_app_switch_step(1);
		tb.emitted_steps++;
	} else {
		queue_app_switch_step(-1);
		tb.emitted_steps--;
	}
}

static bool desktop_delay_edge(uint32_t now_ms, int16_t target_steps)
{
	int16_t ed = edge_direction(tb.current_position);
	if (ed == 0) {
		reset_edge_hold();
		return false;
	}
	int16_t pending_dir = target_steps > tb.emitted_steps ? 1 : -1;
	if (pending_dir != ed) {
		return false;
	}
	if (tb.edge_hold_direction != ed) {
		arm_edge_hold(now_ms, ed);
		return false;
	}
	return now_ms - tb.edge_hold_start_ms < TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS;
}

static void handle_desktop_switch_mode(uint32_t now_ms)
{
	if (!tb.is_desktop_seek_mode) {
		if (now_ms - tb.touch_start_ms < TOUCHBAR_DESKTOP_HOLD_MS) {
			return;
		}
		tb.is_desktop_seek_mode = true;
		tb.anchor_position = tb.current_position;
		tb.emitted_steps = 0;
		reset_edge_hold();
		tb.last_step_ms = now_ms - TOUCHBAR_DESKTOP_STEP_INTERVAL_MS;
		return;
	}
	int16_t displacement = tb.current_position - tb.anchor_position;
	int16_t target_steps = displacement / TOUCHBAR_DESKTOP_STEP_DISTANCE;
	if (target_steps == tb.emitted_steps) {
		try_repeat_at_edge(now_ms, TOUCHBAR_DESKTOP_EDGE_REPEAT_DELAY_MS,
				   TOUCHBAR_DESKTOP_STEP_INTERVAL_MS, TOUCHBAR_DESKTOP_STEP_DISTANCE,
				   queue_desktop_switch_step);
		return;
	}
	if (desktop_delay_edge(now_ms, target_steps)) {
		return;
	}
	if (now_ms - tb.last_step_ms < TOUCHBAR_DESKTOP_STEP_INTERVAL_MS) {
		return;
	}
	tb.last_step_ms = now_ms;
	if (edge_direction(tb.current_position) == 0) {
		reset_edge_hold();
	}
	if (target_steps > tb.emitted_steps) {
		queue_desktop_switch_step(1);
		tb.emitted_steps++;
	} else {
		queue_desktop_switch_step(-1);
		tb.emitted_steps--;
	}
}

static void finalize_desktop_gesture(void)
{
	if (!tb.is_gesture_active || tb.is_desktop_seek_mode) {
		return;
	}
	int16_t displacement = tb.current_position - tb.anchor_position;
	if (abs16(displacement) < TOUCHBAR_DESKTOP_SWIPE_DISTANCE) {
		return;
	}
	queue_desktop_switch_step(displacement > 0 ? 1 : -1);
}

static void process_touchbar(uint32_t now_ms)
{
	uint8_t touch_state = raw_touch_state();
	uint8_t active_segment = TOUCHBAR_INVALID_SEGMENT;
	uint8_t active_touch_count = 0;
	int16_t touch_position = -1;

	if (tb.is_touching) {
		active_segment = tb.active_segment;
		if (active_segment < TOUCHBAR_SEGMENT_COUNT) {
			touch_position = get_segment_position(touch_state, active_segment);
			active_touch_count = count_segment_touches(touch_state, active_segment);
		}
	} else {
		active_segment = select_segment(touch_state);
		if (active_segment < TOUCHBAR_SEGMENT_COUNT) {
			touch_position = get_segment_position(touch_state, active_segment);
			active_touch_count = count_segment_touches(touch_state, active_segment);
		}
	}

	if (touch_position < 0) {
		if (!tb.is_touching) {
			return;
		}
		if (!tb.is_no_touch_pending) {
			tb.is_no_touch_pending = true;
			tb.last_touch_ms = now_ms;
			return;
		}
		if (now_ms - tb.last_touch_ms < release_grace_ms()) {
			return;
		}
		if (tb.mode == TOUCHBAR_MODE_DESKTOP_SWITCH) {
			finalize_desktop_gesture();
		}
		clear_touchbar_actions();
		return;
	}

	tb.is_no_touch_pending = false;
	tb.last_touch_ms = now_ms;
	tb.current_position = touch_position;

	if (tb.mode == TOUCHBAR_MODE_APP_SWITCH) {
		bool jitter = tb.is_gesture_active && tb.active_touch_count > active_touch_count &&
			      active_touch_count == 1;
		if (jitter) {
			tb.app_switch_release_guard_until_ms = now_ms + TOUCHBAR_APP_RELEASE_SETTLE_MS;
		} else if (active_touch_count > 1) {
			tb.app_switch_release_guard_until_ms = 0;
		}
	}

	if (!tb.is_touching) {
		tb.is_touching = true;
		tb.active_segment = active_segment;
		tb.active_touch_count = active_touch_count;
		tb.touch_start_ms = now_ms;
		tb.last_touch_ms = now_ms;
		tb.anchor_position = touch_position;
		tb.current_position = touch_position;
		tb.emitted_steps = 0;
		tb.is_desktop_seek_mode = false;
		tb.last_pan_ms = now_ms;
		tb.last_step_ms = now_ms;
		reset_edge_hold();
		tb.app_switch_release_guard_until_ms = 0;
		return;
	}

	tb.active_touch_count = active_touch_count;
	if (!tb.is_gesture_active) {
		if (now_ms - tb.touch_start_ms < activation_delay_ms()) {
			return;
		}
		tb.is_gesture_active = true;
	}

	switch (tb.mode) {
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

static void touchbar_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	uint32_t now = k_uptime_get_32();

	if (zmk_keymap_layer_active(LAYER_FN)) {
		bool rctrl = pos_down[POS_RCTRL];
		if (rctrl && !prev_rctrl) {
			cycle_touchbar_mode();
		}
		prev_rctrl = rctrl;
		clear_touchbar_actions();
		k_work_schedule(&touchbar_work, K_MSEC(10));
		return;
	}
	prev_rctrl = pos_down[POS_RCTRL];

	process_touchbar(now);
	k_work_schedule(&touchbar_work, K_MSEC(10));
}

static void touchbar_schedule(void)
{
	k_work_schedule(&touchbar_work, K_MSEC(10));
}

static int pos_listener(const zmk_event_t *eh)
{
	const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
	if (!ev || ev->position >= 88) {
		return -ENOTSUP;
	}
	pos_down[ev->position] = ev->state;
	touchbar_schedule();
	return 0;
}

static int layer_listener(const zmk_event_t *eh)
{
	ARG_UNUSED(eh);
	touchbar_schedule();
	return 0;
}

ZMK_LISTENER(touchbar_pos, pos_listener);
ZMK_SUBSCRIPTION(touchbar_pos, zmk_position_state_changed);

ZMK_LISTENER(touchbar_layer, layer_listener);
ZMK_SUBSCRIPTION(touchbar_layer, zmk_layer_state_changed);

static int touchbar_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	k_work_init_delayable(&touchbar_work, touchbar_work_handler);
	k_work_schedule(&touchbar_work, K_MSEC(100));
	return 0;
}

SYS_INIT(touchbar_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
