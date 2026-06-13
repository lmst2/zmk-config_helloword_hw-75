/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <app/diag_log.h>

#define HW75_DIAG_MODULE_COUNT ((size_t)(HW75_DIAG_MODULE_SETTINGS + 1))

struct diag_log_state {
	struct k_spinlock lock;
	uint32_t next_seq;
	uint32_t next_trace_id;
	uint32_t dropped_count;
	uint32_t head;
	uint32_t count;
	enum hw75_diag_level min_level;
	uint32_t enabled_modules_mask;
	struct hw75_diag_event ring[CONFIG_HW75_DIAG_LOG_RING_SIZE];
	struct hw75_diag_event boot_events[HW75_DIAG_BOOT_EVENT_LIMIT];
	size_t boot_event_count;
	struct hw75_diag_snapshot snapshots[HW75_DIAG_MODULE_COUNT];
};

static struct diag_log_state diag_state = {
	.next_seq = 1U,
	.next_trace_id = 1U,
	.min_level = HW75_DIAG_LEVEL_DEBUG,
	.enabled_modules_mask = BIT_MASK(HW75_DIAG_MODULE_COUNT),
};

static bool hw75_diag_is_enabled(enum hw75_diag_level level, enum hw75_diag_module module) {
	if ((uint32_t)module >= HW75_DIAG_MODULE_COUNT) {
		return false;
	}

	if ((diag_state.enabled_modules_mask & BIT(module)) == 0U) {
		return false;
	}

	return level <= diag_state.min_level;
}

static bool hw75_diag_events_equal(const struct hw75_diag_event *lhs,
				   const struct hw75_diag_event *rhs) {
	return lhs->level == rhs->level && lhs->module == rhs->module &&
	       lhs->event_id == rhs->event_id && lhs->has_trace_id == rhs->has_trace_id &&
	       lhs->trace_id == rhs->trace_id && lhs->data0 == rhs->data0 &&
	       lhs->has_data1 == rhs->has_data1 && lhs->data1 == rhs->data1;
}

static void hw75_diag_append_event(struct hw75_diag_event *event) {
	if (diag_state.count > 0U) {
		uint32_t last_index =
			(diag_state.head + diag_state.count - 1U) % CONFIG_HW75_DIAG_LOG_RING_SIZE;
		struct hw75_diag_event *last = &diag_state.ring[last_index];
		if (hw75_diag_events_equal(last, event)) {
			if (last->repeat_count < UINT32_MAX) {
				last->repeat_count++;
			}
			last->uptime_ms = event->uptime_ms;
			return;
		}
	}

	if (diag_state.count == CONFIG_HW75_DIAG_LOG_RING_SIZE) {
		diag_state.head = (diag_state.head + 1U) % CONFIG_HW75_DIAG_LOG_RING_SIZE;
		diag_state.dropped_count++;
		diag_state.count--;
	}

	uint32_t index = (diag_state.head + diag_state.count) % CONFIG_HW75_DIAG_LOG_RING_SIZE;
	diag_state.ring[index] = *event;
	diag_state.count++;

	if (diag_state.boot_event_count < HW75_DIAG_BOOT_EVENT_LIMIT) {
		diag_state.boot_events[diag_state.boot_event_count++] = *event;
	}
}

uint32_t hw75_diag_next_trace_id(void) {
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	uint32_t trace_id = diag_state.next_trace_id++;
	k_spin_unlock(&diag_state.lock, key);
	return trace_id;
}

void hw75_diag_log_event(enum hw75_diag_level level, enum hw75_diag_module module,
			 uint32_t trace_id, enum hw75_diag_event_id event_id, uint32_t data0,
			 bool has_data1, uint32_t data1) {
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);

	if (!hw75_diag_is_enabled(level, module)) {
		k_spin_unlock(&diag_state.lock, key);
		return;
	}

	struct hw75_diag_event entry = {
		.seq = diag_state.next_seq++,
		.uptime_ms = k_uptime_get_32(),
		.level = level,
		.module = module,
		.event_id = event_id,
		.trace_id = trace_id,
		.data0 = data0,
		.data1 = data1,
		.repeat_count = 1U,
		.has_trace_id = trace_id != 0U,
		.has_data1 = has_data1,
	};

	hw75_diag_append_event(&entry);
	k_spin_unlock(&diag_state.lock, key);
}

void hw75_diag_update_snapshot(enum hw75_diag_module module, uint32_t state0, uint32_t state1,
			       uint32_t state2) {
	if ((uint32_t)module >= HW75_DIAG_MODULE_COUNT) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	struct hw75_diag_snapshot *snapshot = &diag_state.snapshots[module];
	snapshot->valid = true;
	snapshot->module = module;
	snapshot->updated_ms = k_uptime_get_32();
	snapshot->state0 = state0;
	snapshot->state1 = state1;
	snapshot->state2 = state2;
	k_spin_unlock(&diag_state.lock, key);
}

void hw75_diag_set_config(const struct hw75_diag_config *config) {
	enum hw75_diag_level min_level;
	uint32_t enabled_modules_mask;

	if (config == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	if (config->has_min_level) {
		diag_state.min_level = config->min_level;
	}
	if (config->has_enabled_modules_mask) {
		diag_state.enabled_modules_mask =
			config->enabled_modules_mask & BIT_MASK(HW75_DIAG_MODULE_COUNT);
	}
	min_level = diag_state.min_level;
	enabled_modules_mask = diag_state.enabled_modules_mask;
	k_spin_unlock(&diag_state.lock, key);

	hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_SYSTEM, 0U,
			    HW75_DIAG_EVENT_SYSTEM_LOG_CONFIG,
			    hw75_diag_pack_u16x2((uint16_t)min_level,
						 (uint16_t)enabled_modules_mask),
			    false, 0U);
}

void hw75_diag_clear_events(void) {
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	diag_state.count = 0U;
	diag_state.head = 0U;
	diag_state.dropped_count = 0U;
	k_spin_unlock(&diag_state.lock, key);
}

void hw75_diag_get_config_view(struct hw75_diag_config_view *view) {
	if (view == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	view->min_level = diag_state.min_level;
	view->enabled_modules_mask = diag_state.enabled_modules_mask;
	view->dropped_count = diag_state.dropped_count;
	view->oldest_seq = diag_state.count > 0U ? diag_state.ring[diag_state.head].seq : 0U;
	view->newest_seq = diag_state.count > 0U
				 ? diag_state.ring[(diag_state.head + diag_state.count - 1U) %
						      CONFIG_HW75_DIAG_LOG_RING_SIZE]
					       .seq
				 : 0U;
	k_spin_unlock(&diag_state.lock, key);
}

size_t hw75_diag_get_snapshot_count(void) {
	size_t count = 0U;
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	for (size_t i = 0; i < HW75_DIAG_MODULE_COUNT; i++) {
		if (diag_state.snapshots[i].valid) {
			count++;
		}
	}
	k_spin_unlock(&diag_state.lock, key);
	return count;
}

bool hw75_diag_copy_snapshot_at(size_t index, struct hw75_diag_snapshot *dst) {
	if (dst == NULL) {
		return false;
	}

	bool found = false;
	size_t current = 0U;
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	for (size_t i = 0; i < HW75_DIAG_MODULE_COUNT; i++) {
		if (!diag_state.snapshots[i].valid) {
			continue;
		}
		if (current++ != index) {
			continue;
		}
		*dst = diag_state.snapshots[i];
		found = true;
		break;
	}
	k_spin_unlock(&diag_state.lock, key);
	return found;
}

size_t hw75_diag_get_boot_event_count(void) {
	size_t count;
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	count = diag_state.boot_event_count;
	k_spin_unlock(&diag_state.lock, key);
	return count;
}

bool hw75_diag_copy_boot_event_at(size_t index, struct hw75_diag_event *dst) {
	if (dst == NULL) {
		return false;
	}

	bool found = false;
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	if (index < diag_state.boot_event_count) {
		*dst = diag_state.boot_events[index];
		found = true;
	}
	k_spin_unlock(&diag_state.lock, key);
	return found;
}

bool hw75_diag_copy_event_after_at(uint32_t after_seq, size_t index, struct hw75_diag_event *dst) {
	if (dst == NULL) {
		return false;
	}

	bool found = false;
	size_t current = 0U;
	k_spinlock_key_t key = k_spin_lock(&diag_state.lock);
	for (uint32_t i = 0U; i < diag_state.count; i++) {
		const struct hw75_diag_event *event =
			&diag_state.ring[(diag_state.head + i) % CONFIG_HW75_DIAG_LOG_RING_SIZE];
		if (event->seq <= after_seq) {
			continue;
		}
		if (current++ != index) {
			continue;
		}
		*dst = *event;
		found = true;
		break;
	}
	k_spin_unlock(&diag_state.lock, key);
	return found;
}

static int hw75_diag_init(const struct device *dev) {
	ARG_UNUSED(dev);

	hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_SYSTEM, 0U,
			    HW75_DIAG_EVENT_SYSTEM_INIT,
			    hw75_diag_pack_u16x2(CONFIG_HW75_DIAG_LOG_RING_SIZE,
						 CONFIG_HW75_DIAG_LOG_BOOT_EVENT_COUNT),
			    false, 0U);
	hw75_diag_update_snapshot(HW75_DIAG_MODULE_SYSTEM,
				  hw75_diag_pack_u16x2(CONFIG_HW75_DIAG_LOG_RING_SIZE,
						       CONFIG_HW75_DIAG_LOG_BOOT_EVENT_COUNT),
				  0U, 0U);

	return 0;
}

SYS_INIT(hw75_diag_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
