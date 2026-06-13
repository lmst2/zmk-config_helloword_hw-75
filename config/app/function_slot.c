/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/workqueue.h>

#include <app/function_slot.h>

struct hw75_function_slot_settings_blob {
	uint8_t version;
	struct hw75_function_slot_config slots[HW75_FUNCTION_SLOT_COUNT];
};

struct hw75_function_slot_config_v1 {
	uint8_t slot_index;
	uint8_t slot_type;
	uint8_t flags;
	uint16_t action_code;
	int16_t arg0;
	int16_t arg1;
	int16_t arg2;
	uint8_t macro_step_count;
	struct hw75_function_slot_macro_step macro_steps[HW75_FUNCTION_SLOT_MAX_MACRO_STEPS];
};

struct hw75_function_slot_settings_blob_v1 {
	uint8_t version;
	struct hw75_function_slot_config_v1 slots[HW75_FUNCTION_SLOT_COUNT];
};

struct hw75_function_slot_macro_runtime {
	struct k_work_delayable work;
	bool active;
	uint8_t slot_index;
	uint8_t step_index;
	bool tap_release_pending;
	uint8_t tap_modifiers;
	uint8_t tap_usage_id;
};

static struct k_mutex function_slot_lock;
static struct hw75_function_slot_config function_slots[HW75_FUNCTION_SLOT_COUNT];
static struct hw75_function_slot_event helper_events[HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE];
static uint8_t helper_event_head;
static uint8_t helper_event_count;
static uint16_t helper_event_next_seq;
static uint16_t helper_event_dropped;
static bool function_slot_settings_migrated;
static struct hw75_function_slot_macro_runtime macro_runtime;

#define HW75_FUNCTION_SLOT_SETTINGS_VERSION 2U
#define HW75_FUNCTION_SLOT_SETTINGS_VERSION_V1 1U
#define HW75_FUNCTION_SLOT_TAP_DURATION_MS 18U

static const uint16_t supported_hid_presets[] = {
	HW75_FUNCTION_SLOT_HID_PRESET_MUTE,
	HW75_FUNCTION_SLOT_HID_PRESET_VOL_UP,
	HW75_FUNCTION_SLOT_HID_PRESET_VOL_DOWN,
	HW75_FUNCTION_SLOT_HID_PRESET_PLAY_PAUSE,
	HW75_FUNCTION_SLOT_HID_PRESET_PREV,
	HW75_FUNCTION_SLOT_HID_PRESET_NEXT,
	HW75_FUNCTION_SLOT_HID_PRESET_STOP,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_SLEEP,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_WAKE,
	HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_POWER,
	HW75_FUNCTION_SLOT_HID_PRESET_CALCULATOR,
	HW75_FUNCTION_SLOT_HID_PRESET_MAIL,
	HW75_FUNCTION_SLOT_HID_PRESET_BROWSER,
	HW75_FUNCTION_SLOT_HID_PRESET_FILE_BROWSER,
	HW75_FUNCTION_SLOT_HID_PRESET_IMAGE_BROWSER,
	HW75_FUNCTION_SLOT_HID_PRESET_MUSIC_BROWSER,
};

static uint32_t function_slot_keyboard_encoded(uint8_t usage_id, uint8_t modifiers) {
	return APPLY_MODS(modifiers, ZMK_HID_USAGE(HID_USAGE_KEY, usage_id));
}

static int function_slot_emit_encoded(uint32_t encoded, bool pressed, int64_t timestamp) {
	return ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(encoded, pressed, timestamp));
}

static void function_slot_emit_modifier_mask(uint8_t modifiers, bool pressed, int64_t timestamp) {
	static const struct {
		uint8_t mask;
		uint8_t usage_id;
	} modifier_map[] = {
		{MOD_LCTL, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL},
		{MOD_LSFT, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT},
		{MOD_LALT, HID_USAGE_KEY_KEYBOARD_LEFTALT},
		{MOD_LGUI, HID_USAGE_KEY_KEYBOARD_LEFT_GUI},
		{MOD_RCTL, HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL},
		{MOD_RSFT, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT},
		{MOD_RALT, HID_USAGE_KEY_KEYBOARD_RIGHTALT},
		{MOD_RGUI, HID_USAGE_KEY_KEYBOARD_RIGHT_GUI},
	};

	for (size_t index = 0U; index < ARRAY_SIZE(modifier_map); index++) {
		if ((modifiers & modifier_map[index].mask) == 0U) {
			continue;
		}

		function_slot_emit_encoded(
			function_slot_keyboard_encoded(modifier_map[index].usage_id, 0U), pressed,
			timestamp);
	}
}

static uint32_t function_slot_hid_preset_encoded(uint16_t action_code) {
	switch (action_code) {
	case HW75_FUNCTION_SLOT_HID_PRESET_MUTE:
		return C_MUTE;
	case HW75_FUNCTION_SLOT_HID_PRESET_VOL_UP:
		return C_VOL_UP;
	case HW75_FUNCTION_SLOT_HID_PRESET_VOL_DOWN:
		return C_VOL_DN;
	case HW75_FUNCTION_SLOT_HID_PRESET_PLAY_PAUSE:
		return C_PP;
	case HW75_FUNCTION_SLOT_HID_PRESET_PREV:
		return C_PREV;
	case HW75_FUNCTION_SLOT_HID_PRESET_NEXT:
		return C_NEXT;
	case HW75_FUNCTION_SLOT_HID_PRESET_STOP:
		return C_STOP;
	case HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_SLEEP:
		return SYS_SLEEP;
	case HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_WAKE:
		return SYS_WAKE;
	case HW75_FUNCTION_SLOT_HID_PRESET_SYSTEM_POWER:
		return SYS_PWR;
	case HW75_FUNCTION_SLOT_HID_PRESET_CALCULATOR:
		return C_AL_CALC;
	case HW75_FUNCTION_SLOT_HID_PRESET_MAIL:
		return C_AL_MAIL;
	case HW75_FUNCTION_SLOT_HID_PRESET_BROWSER:
		return C_AL_WWW;
	case HW75_FUNCTION_SLOT_HID_PRESET_FILE_BROWSER:
		return C_AL_FILES;
	case HW75_FUNCTION_SLOT_HID_PRESET_IMAGE_BROWSER:
		return C_AL_IMAGES;
	case HW75_FUNCTION_SLOT_HID_PRESET_MUSIC_BROWSER:
		return C_AL_MUSIC;
	default:
		return 0U;
	}
}

static void function_slot_apply_combo_press(const struct hw75_function_slot_config *config,
					    int64_t timestamp) {
	uint8_t modifiers = (uint8_t)CLAMP(config->arg0, 0, 0xFF);
	uint8_t usage_id = (uint8_t)CLAMP(config->action_code, 0, 0xFF);
	function_slot_emit_modifier_mask(modifiers, true, timestamp);
	function_slot_emit_encoded(function_slot_keyboard_encoded(usage_id, 0U), true, timestamp);
}

static void function_slot_apply_combo_release(const struct hw75_function_slot_config *config,
					      int64_t timestamp) {
	uint8_t modifiers = (uint8_t)CLAMP(config->arg0, 0, 0xFF);
	uint8_t usage_id = (uint8_t)CLAMP(config->action_code, 0, 0xFF);
	function_slot_emit_encoded(function_slot_keyboard_encoded(usage_id, 0U), false, timestamp);
	function_slot_emit_modifier_mask(modifiers, false, timestamp);
}

static void function_slot_macro_emit_step(const struct hw75_function_slot_macro_step *step, bool pressed,
					  int64_t timestamp) {
	if (step->usage_id == 0U) {
		return;
	}

	function_slot_emit_modifier_mask(step->modifiers, pressed, timestamp);
	function_slot_emit_encoded(function_slot_keyboard_encoded(step->usage_id, 0U), pressed,
				  timestamp);
}

static bool function_slot_hid_preset_supported(uint16_t action_code) {
	for (size_t index = 0U; index < ARRAY_SIZE(supported_hid_presets); index++) {
		if (supported_hid_presets[index] == action_code) {
			return true;
		}
	}

	return false;
}

static uint8_t function_slot_compute_checksum(const struct hw75_function_slot_config *config) {
	uint8_t checksum = 0x5AU;

	checksum = (uint8_t)((checksum * 33U) ^ config->slot_index);
	checksum = (uint8_t)((checksum * 33U) ^ config->slot_type);
	checksum = (uint8_t)((checksum * 33U) ^ config->flags);
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(config->action_code & 0xFFU));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(config->action_code >> 8));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(config->arg0 & 0xFF));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(((uint16_t)config->arg0 >> 8) & 0xFFU));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(config->arg1 & 0xFF));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(((uint16_t)config->arg1 >> 8) & 0xFFU));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(config->arg2 & 0xFF));
	checksum = (uint8_t)((checksum * 33U) ^ (uint8_t)(((uint16_t)config->arg2 >> 8) & 0xFFU));
	checksum = (uint8_t)((checksum * 33U) ^ config->macro_step_count);

	for (uint8_t index = 0U; index < config->macro_step_count; index++) {
		const struct hw75_function_slot_macro_step *step = &config->macro_steps[index];
		checksum = (uint8_t)((checksum * 33U) ^ step->type);
		checksum = (uint8_t)((checksum * 33U) ^ step->modifiers);
		checksum = (uint8_t)((checksum * 33U) ^ step->usage_id);
		checksum = (uint8_t)((checksum * 33U) ^ step->delay_ms);
	}

	return checksum;
}

static void function_slot_normalize_config(struct hw75_function_slot_config *config) {
	config->checksum = function_slot_compute_checksum(config);
}

static bool function_slot_checksum_valid(const struct hw75_function_slot_config *config) {
	return config->checksum == function_slot_compute_checksum(config);
}

static void function_slot_default_config(uint8_t slot_index, struct hw75_function_slot_config *config) {
	static const uint8_t default_usage_ids[HW75_FUNCTION_SLOT_COUNT] = {
		HID_USAGE_KEY_KEYBOARD_PAUSE,
		HID_USAGE_KEY_KEYBOARD_INSERT,
		HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD,
		HID_USAGE_KEY_KEYBOARD_PAGEUP,
		HID_USAGE_KEY_KEYBOARD_PAGEDOWN,
	};

	*config = (struct hw75_function_slot_config){
		.slot_index = slot_index,
		.slot_type = HW75_FUNCTION_SLOT_TYPE_KEY_COMBO,
		.flags = 0U,
		.action_code = default_usage_ids[slot_index],
		.arg0 = 0,
		.arg1 = 0,
		.arg2 = 0,
		.macro_step_count = 0U,
	};
	function_slot_normalize_config(config);
}

static void function_slot_reset_defaults(void) {
	for (uint8_t slot_index = 0U; slot_index < HW75_FUNCTION_SLOT_COUNT; slot_index++) {
		function_slot_default_config(slot_index, &function_slots[slot_index]);
	}
}

static bool function_slot_validate_step(const struct hw75_function_slot_macro_step *step) {
	switch (step->type) {
	case HW75_FUNCTION_SLOT_MACRO_STEP_TAP:
	case HW75_FUNCTION_SLOT_MACRO_STEP_DOWN:
	case HW75_FUNCTION_SLOT_MACRO_STEP_UP:
		return step->usage_id != 0U;
	case HW75_FUNCTION_SLOT_MACRO_STEP_DELAY:
		return step->delay_ms > 0U;
	default:
		return false;
	}
}

static bool function_slot_validate_config_semantics(const struct hw75_function_slot_config *config) {
	if (config == NULL || config->slot_index >= HW75_FUNCTION_SLOT_COUNT) {
		return false;
	}

	switch (config->slot_type) {
	case HW75_FUNCTION_SLOT_TYPE_NONE:
		return true;
	case HW75_FUNCTION_SLOT_TYPE_HID_PRESET:
		return function_slot_hid_preset_supported(config->action_code);
	case HW75_FUNCTION_SLOT_TYPE_KEY_COMBO:
		return config->action_code > 0U && config->action_code <= 0xFFU &&
		       config->arg0 >= 0 && config->arg0 <= 0xFF;
	case HW75_FUNCTION_SLOT_TYPE_MACRO_SEQ:
		if (config->macro_step_count > HW75_FUNCTION_SLOT_MAX_MACRO_STEPS) {
			return false;
		}
		for (uint8_t index = 0U; index < config->macro_step_count; index++) {
			if (!function_slot_validate_step(&config->macro_steps[index])) {
				return false;
			}
		}
		return config->macro_step_count > 0U;
	case HW75_FUNCTION_SLOT_TYPE_HELPER_ACTION:
		return config->action_code != 0U;
	default:
		return false;
	}
}

static void function_slot_upgrade_v1_config(uint8_t slot_index,
					    const struct hw75_function_slot_config_v1 *src,
					    struct hw75_function_slot_config *dst) {
	*dst = (struct hw75_function_slot_config){
		.slot_index = slot_index,
		.slot_type = src->slot_type,
		.flags = src->flags,
		.action_code = src->action_code,
		.arg0 = src->arg0,
		.arg1 = src->arg1,
		.arg2 = src->arg2,
		.macro_step_count = src->macro_step_count,
	};
	memcpy(dst->macro_steps, src->macro_steps, sizeof(src->macro_steps));
	function_slot_normalize_config(dst);
}

static int function_slot_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
					  void *cb_arg, void *param) {
	ARG_UNUSED(param);

	const char *next;
	struct hw75_function_slot_settings_blob blob;
	struct hw75_function_slot_settings_blob_v1 blob_v1;

	if (!settings_name_steq(name, "slots", &next) || next != NULL) {
		return -ENOENT;
	}

	if (len == sizeof(blob)) {
		int ret = read_cb(cb_arg, &blob, sizeof(blob));
		if (ret < 0) {
			return ret;
		}

		if (blob.version != HW75_FUNCTION_SLOT_SETTINGS_VERSION) {
			return -EINVAL;
		}

		for (uint8_t slot_index = 0U; slot_index < HW75_FUNCTION_SLOT_COUNT; slot_index++) {
			if (!function_slot_validate_config_semantics(&blob.slots[slot_index]) ||
			    !function_slot_checksum_valid(&blob.slots[slot_index])) {
				return -EINVAL;
			}
		}

		memcpy(function_slots, blob.slots, sizeof(function_slots));
		return 0;
	}

	if (len == sizeof(blob_v1)) {
		int ret = read_cb(cb_arg, &blob_v1, sizeof(blob_v1));
		if (ret < 0) {
			return ret;
		}

		if (blob_v1.version != HW75_FUNCTION_SLOT_SETTINGS_VERSION_V1) {
			return -EINVAL;
		}

		for (uint8_t slot_index = 0U; slot_index < HW75_FUNCTION_SLOT_COUNT; slot_index++) {
			struct hw75_function_slot_config upgraded;
			function_slot_upgrade_v1_config(slot_index, &blob_v1.slots[slot_index], &upgraded);
			if (!function_slot_validate_config_semantics(&upgraded)) {
				return -EINVAL;
			}
			function_slots[slot_index] = upgraded;
		}

		function_slot_settings_migrated = true;
		return 0;
	}

	return -EINVAL;
}

static void function_slot_save_work_handler(struct k_work *work) {
	ARG_UNUSED(work);

	struct hw75_function_slot_settings_blob blob = {
		.version = HW75_FUNCTION_SLOT_SETTINGS_VERSION,
	};

	memcpy(blob.slots, function_slots, sizeof(function_slots));

	int ret = settings_save_one("app/function_slots/slots", &blob, sizeof(blob));
	if (ret != 0) {
		LOG_ERR("Failed saving function slot settings: %d", ret);
	}
}

K_WORK_DELAYABLE_DEFINE(function_slot_save_work, function_slot_save_work_handler);

static int function_slot_schedule_save(void) {
#ifdef CONFIG_SETTINGS
	return MIN(k_work_reschedule(&function_slot_save_work,
				    K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE)),
		   0);
#else
	return 0;
#endif
}

static void function_slot_enqueue_helper_event(const struct hw75_function_slot_config *config) {
	uint8_t insert_index;
	struct hw75_function_slot_event *event;

	if (helper_event_count >= HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE) {
		helper_event_head = (helper_event_head + 1U) % HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE;
		helper_event_count--;
		helper_event_dropped++;
	}

	insert_index = (helper_event_head + helper_event_count) % HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE;
	event = &helper_events[insert_index];
	event->seq = ++helper_event_next_seq;
	event->slot_index = config->slot_index;
	event->action_code = config->action_code;
	event->arg0 = config->arg0;
	event->arg1 = config->arg1;
	event->arg2 = config->arg2;
	event->flags = config->flags;
	helper_event_count++;
}

static void function_slot_macro_runtime_stop(void) {
	k_work_cancel_delayable(&macro_runtime.work);
	macro_runtime.active = false;
	macro_runtime.step_index = 0U;
	macro_runtime.tap_release_pending = false;
	macro_runtime.tap_modifiers = 0U;
	macro_runtime.tap_usage_id = 0U;
}

static void function_slot_macro_runtime_schedule(uint32_t delay_ms);

static void function_slot_macro_runtime_work(struct k_work *work) {
	ARG_UNUSED(work);

	struct hw75_function_slot_config config;
	int64_t timestamp = k_uptime_get();

	if (!macro_runtime.active ||
	    hw75_function_slot_get_config(macro_runtime.slot_index, &config) != 0 ||
	    config.slot_type != HW75_FUNCTION_SLOT_TYPE_MACRO_SEQ) {
		function_slot_macro_runtime_stop();
		return;
	}

	if (macro_runtime.tap_release_pending) {
		struct hw75_function_slot_macro_step step = {
			.type = HW75_FUNCTION_SLOT_MACRO_STEP_TAP,
			.modifiers = macro_runtime.tap_modifiers,
			.usage_id = macro_runtime.tap_usage_id,
		};
		function_slot_macro_emit_step(&step, false, timestamp);
		macro_runtime.tap_release_pending = false;
		macro_runtime.step_index++;
	}

	while (macro_runtime.step_index < config.macro_step_count) {
		const struct hw75_function_slot_macro_step *step =
			&config.macro_steps[macro_runtime.step_index];

		switch (step->type) {
		case HW75_FUNCTION_SLOT_MACRO_STEP_TAP:
			function_slot_macro_emit_step(step, true, timestamp);
			macro_runtime.tap_release_pending = true;
			macro_runtime.tap_modifiers = step->modifiers;
			macro_runtime.tap_usage_id = step->usage_id;
			function_slot_macro_runtime_schedule(HW75_FUNCTION_SLOT_TAP_DURATION_MS);
			return;
		case HW75_FUNCTION_SLOT_MACRO_STEP_DOWN:
			function_slot_macro_emit_step(step, true, timestamp);
			macro_runtime.step_index++;
			break;
		case HW75_FUNCTION_SLOT_MACRO_STEP_UP:
			function_slot_macro_emit_step(step, false, timestamp);
			macro_runtime.step_index++;
			break;
		case HW75_FUNCTION_SLOT_MACRO_STEP_DELAY:
			macro_runtime.step_index++;
			function_slot_macro_runtime_schedule(step->delay_ms);
			return;
		default:
			function_slot_macro_runtime_stop();
			return;
		}
	}

	function_slot_macro_runtime_stop();
}

static void function_slot_macro_runtime_schedule(uint32_t delay_ms) {
	k_work_reschedule(&macro_runtime.work, K_MSEC(MAX(delay_ms, 1U)));
}

static int function_slot_run_local_action(const struct hw75_function_slot_config *config, bool pressed,
					  int64_t timestamp) {
	if (config == NULL) {
		return -EINVAL;
	}

	switch (config->slot_type) {
	case HW75_FUNCTION_SLOT_TYPE_NONE:
		return 0;
	case HW75_FUNCTION_SLOT_TYPE_HID_PRESET: {
		uint32_t encoded = function_slot_hid_preset_encoded(config->action_code);
		if (encoded == 0U) {
			return -EINVAL;
		}
		return function_slot_emit_encoded(encoded, pressed, timestamp);
	}
	case HW75_FUNCTION_SLOT_TYPE_KEY_COMBO:
		if (pressed) {
			function_slot_apply_combo_press(config, timestamp);
		} else {
			function_slot_apply_combo_release(config, timestamp);
		}
		return 0;
	case HW75_FUNCTION_SLOT_TYPE_MACRO_SEQ:
		if (!pressed) {
			return 0;
		}
		function_slot_macro_runtime_stop();
		macro_runtime.active = true;
		macro_runtime.slot_index = config->slot_index;
		macro_runtime.step_index = 0U;
		function_slot_macro_runtime_schedule(1U);
		return 0;
	case HW75_FUNCTION_SLOT_TYPE_HELPER_ACTION:
		if (pressed) {
			k_mutex_lock(&function_slot_lock, K_FOREVER);
			function_slot_enqueue_helper_event(config);
			k_mutex_unlock(&function_slot_lock);
		}
		return 0;
	default:
		return -EINVAL;
	}
}

int hw75_function_slot_get_caps(struct hw75_function_slot_caps *caps) {
	if (caps == NULL) {
		return -EINVAL;
	}

	memset(caps, 0, sizeof(*caps));
	caps->slot_count = HW75_FUNCTION_SLOT_COUNT;
	caps->max_macro_steps = HW75_FUNCTION_SLOT_MAX_MACRO_STEPS;
	caps->supported_hid_preset_count = ARRAY_SIZE(supported_hid_presets);
	memcpy(caps->supported_hid_presets, supported_hid_presets, sizeof(supported_hid_presets));
	return 0;
}

int hw75_function_slot_get_config(uint8_t slot_index, struct hw75_function_slot_config *config) {
	if (slot_index >= HW75_FUNCTION_SLOT_COUNT || config == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&function_slot_lock, K_FOREVER);
	*config = function_slots[slot_index];
	k_mutex_unlock(&function_slot_lock);
	return 0;
}

int hw75_function_slot_set_config(const struct hw75_function_slot_config *config) {
	struct hw75_function_slot_config normalized;

	if (!function_slot_validate_config_semantics(config)) {
		return -EINVAL;
	}

	normalized = *config;
	function_slot_normalize_config(&normalized);

	k_mutex_lock(&function_slot_lock, K_FOREVER);
	function_slots[config->slot_index] = normalized;
	k_mutex_unlock(&function_slot_lock);

	return function_slot_schedule_save();
}

int hw75_function_slot_binding_pressed(uint8_t slot_index, int64_t timestamp) {
	struct hw75_function_slot_config config;

	if (hw75_function_slot_get_config(slot_index, &config) != 0) {
		return -EINVAL;
	}

	return function_slot_run_local_action(&config, true, timestamp);
}

int hw75_function_slot_binding_released(uint8_t slot_index, int64_t timestamp) {
	struct hw75_function_slot_config config;

	if (hw75_function_slot_get_config(slot_index, &config) != 0) {
		return -EINVAL;
	}

	return function_slot_run_local_action(&config, false, timestamp);
}

int hw75_function_slot_fetch_events(uint16_t after_seq, uint8_t max_count,
				    struct hw75_function_slot_event_batch *batch) {
	if (batch == NULL) {
		return -EINVAL;
	}

	memset(batch, 0, sizeof(*batch));
	max_count = MIN(max_count, HW75_FUNCTION_SLOT_EVENT_BATCH_MAX);
	if (max_count == 0U) {
		max_count = HW75_FUNCTION_SLOT_EVENT_BATCH_MAX;
	}

	k_mutex_lock(&function_slot_lock, K_FOREVER);

	if (after_seq != 0U) {
		while (helper_event_count > 0U) {
			const struct hw75_function_slot_event *head_event =
				&helper_events[helper_event_head];
			if (head_event->seq > after_seq) {
				break;
			}

			helper_event_head =
				(helper_event_head + 1U) % HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE;
			helper_event_count--;
		}
	}

	batch->dropped_count = helper_event_dropped;
	if (helper_event_count > 0U) {
		batch->oldest_seq = helper_events[helper_event_head].seq;
		batch->newest_seq =
			helper_events[(helper_event_head + helper_event_count - 1U) %
				      HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE]
				.seq;
	}

	for (uint8_t index = 0U; index < helper_event_count && batch->count < max_count; index++) {
		const struct hw75_function_slot_event *event =
			&helper_events[(helper_event_head + index) %
				       HW75_FUNCTION_SLOT_HELPER_EVENT_QUEUE_SIZE];
		if (after_seq != 0U && event->seq <= after_seq) {
			continue;
		}

		batch->events[batch->count++] = *event;
	}
	k_mutex_unlock(&function_slot_lock);

	return 0;
}

static int function_slot_init(const struct device *dev) {
	ARG_UNUSED(dev);

	k_mutex_init(&function_slot_lock);
	function_slot_reset_defaults();

	k_work_init_delayable(&macro_runtime.work, function_slot_macro_runtime_work);

#ifdef CONFIG_SETTINGS
	int ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Failed initializing settings for function slots: %d", ret);
	}

	ret = settings_load_subtree_direct("app/function_slots", function_slot_settings_load_cb, NULL);
	if (ret != 0 && ret != -ENOENT) {
		LOG_ERR("Failed loading function slot settings: %d", ret);
	}
	if (function_slot_settings_migrated) {
		function_slot_schedule_save();
	}
#endif

	return 0;
}

SYS_INIT(function_slot_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
