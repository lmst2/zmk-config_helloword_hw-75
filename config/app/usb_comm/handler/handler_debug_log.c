/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <pb_encode.h>

#include <app/diag_log.h>

#include "handler.h"
#include "usb_comm.pb.h"

/*
 * fill_log_event() / encode_log_snapshot() cast the hand-written C enums in
 * <app/diag_log.h> straight to the nanopb proto enums by integer value, so the
 * two must stay numerically identical. config/proto/check_enum_sync.py verifies
 * every value in CI; these BUILD_ASSERTs are a local compile-time tripwire on
 * the first/last anchor of each enum so drift fails the build immediately.
 */
BUILD_ASSERT((int)HW75_DIAG_LEVEL_ERROR == (int)usb_comm_LogLevel_ERROR);
BUILD_ASSERT((int)HW75_DIAG_LEVEL_TRACE == (int)usb_comm_LogLevel_TRACE);
BUILD_ASSERT((int)HW75_DIAG_MODULE_SYSTEM == (int)usb_comm_LogModule_SYSTEM);
BUILD_ASSERT((int)HW75_DIAG_MODULE_HELPER_CORE == (int)usb_comm_LogModule_HELPER_CORE);
BUILD_ASSERT((int)HW75_DIAG_EVENT_SYSTEM_INIT == (int)usb_comm_LogEventId_LOG_EVENT_SYSTEM_INIT);
BUILD_ASSERT((int)HW75_DIAG_EVENT_HELPER_WEATHER_SYNC ==
	     (int)usb_comm_LogEventId_LOG_EVENT_HELPER_WEATHER_SYNC);

struct log_encode_context {
	size_t snapshot_count;
	size_t boot_event_count;
	size_t event_count;
	struct hw75_diag_config config;
	uint32_t dropped_count;
	uint32_t oldest_seq;
	uint32_t newest_seq;
	uint32_t after_seq;
	struct hw75_diag_snapshot snapshots[HW75_DIAG_MODULE_SETTINGS + 1];
	usb_comm_LogEvent boot_events[HW75_DIAG_BOOT_EVENT_LIMIT];
	usb_comm_LogEvent events[HW75_DIAG_LOG_EVENT_BATCH_MAX];
};

/*
 * USB comm requests are handled on a single worker thread, so LOG_GET_STATE
 * and LOG_GET_EVENTS never encode concurrently. Reusing one scratch context
 * avoids keeping two 536-byte static buffers resident in SRAM.
 */
static struct log_encode_context log_encode_ctx;

static void fill_log_event(usb_comm_LogEvent *dst, const struct hw75_diag_event *src) {
	*dst = (usb_comm_LogEvent)usb_comm_LogEvent_init_zero;
	dst->seq = src->seq;
	dst->uptime_ms = src->uptime_ms;
	dst->level = (usb_comm_LogLevel)src->level;
	dst->module = (usb_comm_LogModule)src->module;
	dst->event_id = (usb_comm_LogEventId)src->event_id;
	dst->data0 = src->data0;
	dst->has_data1 = src->has_data1;
	dst->data1 = src->data1;
	dst->has_trace_id = src->has_trace_id;
	dst->trace_id = src->trace_id;
	dst->has_repeat_count = true;
	dst->repeat_count = src->repeat_count;
}

static bool encode_log_snapshot(pb_ostream_t *stream, const struct hw75_diag_snapshot *src) {
	usb_comm_LogSnapshot snapshot = usb_comm_LogSnapshot_init_zero;
	snapshot.module = (usb_comm_LogModule)src->module;
	snapshot.has_updated_ms = true;
	snapshot.updated_ms = src->updated_ms;
	snapshot.state0 = src->state0;
	snapshot.state1 = src->state1;
	snapshot.state2 = src->state2;

	return pb_encode_submessage(stream, usb_comm_LogSnapshot_fields, &snapshot);
}

static bool encode_log_snapshots_callback(pb_ostream_t *stream, const pb_field_t *field,
					  void *const *arg) {
	const struct log_encode_context *ctx = *arg;

	for (size_t i = 0; i < ctx->snapshot_count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!encode_log_snapshot(stream, &ctx->snapshots[i])) {
			return false;
		}
	}

	return true;
}

static bool encode_log_boot_events_callback(pb_ostream_t *stream, const pb_field_t *field,
					    void *const *arg) {
	const struct log_encode_context *ctx = *arg;

	for (size_t i = 0; i < ctx->boot_event_count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_submessage(stream, usb_comm_LogEvent_fields, &ctx->boot_events[i])) {
			return false;
		}
	}

	return true;
}

static bool encode_log_events_callback(pb_ostream_t *stream, const pb_field_t *field,
				       void *const *arg) {
	const struct log_encode_context *ctx = *arg;

	for (size_t i = 0; i < ctx->event_count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_submessage(stream, usb_comm_LogEvent_fields, &ctx->events[i])) {
			return false;
		}
	}

	return true;
}

static bool handle_log_get_state(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				 const void *bytes, uint32_t bytes_len) {
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	struct log_encode_context *ctx = &log_encode_ctx;

	struct hw75_diag_config_view view;
	hw75_diag_get_config_view(&view);

	ctx->snapshot_count = hw75_diag_get_snapshot_count();
	ctx->boot_event_count = MIN(hw75_diag_get_boot_event_count(), (size_t)HW75_DIAG_BOOT_EVENT_LIMIT);
	ctx->event_count = 0U;
	ctx->config = (struct hw75_diag_config){
		.has_min_level = true,
		.min_level = view.min_level,
		.has_enabled_modules_mask = true,
		.enabled_modules_mask = view.enabled_modules_mask,
	};
	ctx->dropped_count = view.dropped_count;
	ctx->oldest_seq = view.oldest_seq;
	ctx->newest_seq = view.newest_seq;
	for (size_t i = 0; i < ctx->snapshot_count; i++) {
		if (!hw75_diag_copy_snapshot_at(i, &ctx->snapshots[i])) {
			ctx->snapshot_count = i;
			break;
		}
	}
	for (size_t i = 0; i < ctx->boot_event_count; i++) {
		struct hw75_diag_event event;
		if (!hw75_diag_copy_boot_event_at(i, &event)) {
			ctx->boot_event_count = i;
			break;
		}
		fill_log_event(&ctx->boot_events[i], &event);
	}

	usb_comm_LogState *state = &d2h->payload.log_state;
	state->has_config = true;
	state->config = (usb_comm_LogConfig){
		.has_min_level = ctx->config.has_min_level,
		.min_level = (usb_comm_LogLevel)ctx->config.min_level,
		.has_enabled_modules_mask = ctx->config.has_enabled_modules_mask,
		.enabled_modules_mask = ctx->config.enabled_modules_mask,
	};
	state->has_dropped_count = true;
	state->dropped_count = ctx->dropped_count;
	state->has_oldest_seq = true;
	state->oldest_seq = ctx->oldest_seq;
	state->has_newest_seq = true;
	state->newest_seq = ctx->newest_seq;
	if (ctx->snapshot_count > 0U) {
		state->snapshots.funcs.encode = encode_log_snapshots_callback;
		state->snapshots.arg = ctx;
	}
	if (ctx->boot_event_count > 0U) {
		state->boot_events.funcs.encode = encode_log_boot_events_callback;
		state->boot_events.arg = ctx;
	}

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_LOG_GET_STATE, usb_comm_MessageD2H_log_state_tag,
			handle_log_get_state);

static bool handle_log_get_events(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				  const void *bytes, uint32_t bytes_len) {
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	struct log_encode_context *ctx = &log_encode_ctx;

	uint32_t after_seq = h2d->payload.log_request.has_after_seq ? h2d->payload.log_request.after_seq : 0U;
	uint32_t max_count =
		h2d->payload.log_request.has_max_count ? h2d->payload.log_request.max_count :
						 HW75_DIAG_LOG_EVENT_BATCH_MAX;
	max_count = CLAMP(max_count, 1U, HW75_DIAG_LOG_EVENT_BATCH_MAX);

	struct hw75_diag_config_view view;
	hw75_diag_get_config_view(&view);

	ctx->after_seq = after_seq;
	ctx->event_count = max_count;
	ctx->snapshot_count = 0U;
	ctx->boot_event_count = 0U;
	ctx->dropped_count = view.dropped_count;
	ctx->oldest_seq = view.oldest_seq;
	ctx->newest_seq = view.newest_seq;
	for (size_t i = 0; i < max_count; i++) {
		struct hw75_diag_event event;
		if (!hw75_diag_copy_event_after_at(ctx->after_seq, i, &event)) {
			ctx->event_count = i;
			break;
		}
		fill_log_event(&ctx->events[i], &event);
	}

	usb_comm_LogEvents *log_events = &d2h->payload.log_events;
	log_events->events.funcs.encode = encode_log_events_callback;
	log_events->events.arg = ctx;
	log_events->has_dropped_count = true;
	log_events->dropped_count = ctx->dropped_count;
	log_events->has_oldest_seq = true;
	log_events->oldest_seq = ctx->oldest_seq;
	log_events->has_newest_seq = true;
	log_events->newest_seq = ctx->newest_seq;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_LOG_GET_EVENTS, usb_comm_MessageD2H_log_events_tag,
			handle_log_get_events);

static bool handle_log_set_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				  const void *bytes, uint32_t bytes_len) {
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	struct hw75_diag_config config = {
		.has_min_level = h2d->payload.log_config.has_min_level,
		.min_level = (enum hw75_diag_level)h2d->payload.log_config.min_level,
		.has_enabled_modules_mask = h2d->payload.log_config.has_enabled_modules_mask,
		.enabled_modules_mask = h2d->payload.log_config.enabled_modules_mask,
	};

	hw75_diag_set_config(&config);
	return handle_log_get_state(h2d, d2h, NULL, 0U);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_LOG_SET_CONFIG, usb_comm_MessageD2H_log_state_tag,
			handle_log_set_config);

static bool handle_log_clear(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
			     const void *bytes, uint32_t bytes_len) {
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	hw75_diag_clear_events();
	return handle_log_get_state(h2d, d2h, NULL, 0U);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_LOG_CLEAR, usb_comm_MessageD2H_log_state_tag,
			handle_log_clear);
