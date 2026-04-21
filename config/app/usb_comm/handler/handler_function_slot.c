/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/sys/util.h>

#include "handler.h"
#include "usb_comm.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include <app/function_slot.h>

/*
 * The three function-slot messages used to inline fixed-size arrays:
 *   FunctionSlotConfig.macro_steps_packed[6]  (~28 B)
 *   FunctionSlotCaps.supported_hid_presets[16] (~70 B)
 *   FunctionSlotEvents.events[4]              (~130 B)
 *
 * All three are now FT_CALLBACK in usb_comm.keyboard.options so the
 * generated structs only keep a pb_callback_t (8 B). Real data lives in
 * the scratch views below; the handlers wire the callbacks around them.
 * usb_comm is single-threaded, so sharing the scratch across handlers is
 * safe.
 */

static struct hw75_function_slot_config g_slot_cfg_encode;
static struct hw75_function_slot_config g_slot_cfg_decode;
static struct hw75_function_slot_caps g_slot_caps;
static struct hw75_function_slot_event_batch g_slot_events;

/* ---------------- pack/unpack macro steps ---------------- */

static uint32_t pack_macro_step(const struct hw75_function_slot_macro_step *step)
{
	return ((uint32_t)step->type & 0xFFU) |
	       (((uint32_t)step->modifiers & 0xFFU) << 8) |
	       (((uint32_t)step->usage_id & 0xFFU) << 16) |
	       (((uint32_t)step->delay_ms & 0xFFU) << 24);
}

static struct hw75_function_slot_macro_step unpack_macro_step(uint32_t packed)
{
	return (struct hw75_function_slot_macro_step){
		.type = (uint8_t)(packed & 0xFFU),
		.modifiers = (uint8_t)((packed >> 8) & 0xFFU),
		.usage_id = (uint8_t)((packed >> 16) & 0xFFU),
		.delay_ms = (uint8_t)((packed >> 24) & 0xFFU),
	};
}

/* ---------------- macro_steps_packed (repeated uint32) ---------------- */

static bool encode_macro_steps(pb_ostream_t *stream, const pb_field_t *field,
			       void *const *arg)
{
	const struct hw75_function_slot_config *src = *arg;
	for (uint8_t i = 0; i < src->macro_step_count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_varint(stream, pack_macro_step(&src->macro_steps[i]))) {
			return false;
		}
	}
	return true;
}

static bool decode_macro_step(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	struct hw75_function_slot_config *dst = *arg;
	uint64_t packed;
	if (!pb_decode_varint(stream, &packed)) {
		return false;
	}
	if (dst->macro_step_count < HW75_FUNCTION_SLOT_MAX_MACRO_STEPS) {
		dst->macro_steps[dst->macro_step_count++] = unpack_macro_step((uint32_t)packed);
	}
	return true;
}

/* ---------------- supported_hid_presets (repeated uint32) ---------------- */

static bool encode_hid_presets(pb_ostream_t *stream, const pb_field_t *field,
			       void *const *arg)
{
	const struct hw75_function_slot_caps *src = *arg;
	for (uint8_t i = 0; i < src->supported_hid_preset_count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_varint(stream, src->supported_hid_presets[i])) {
			return false;
		}
	}
	return true;
}

/* ---------------- events (repeated FunctionSlotEvent) ---------------- */

static bool encode_events(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct hw75_function_slot_event_batch *batch = *arg;
	for (uint8_t i = 0; i < batch->count; i++) {
		const struct hw75_function_slot_event *src = &batch->events[i];
		usb_comm_FunctionSlotEvent tmp = {
			.seq = src->seq,
			.has_slot_index = true,
			.slot_index = src->slot_index,
			.has_action_code = true,
			.action_code = src->action_code,
			.has_arg0 = true,
			.arg0 = src->arg0,
			.has_arg1 = true,
			.arg1 = src->arg1,
			.has_arg2 = true,
			.arg2 = src->arg2,
			.has_flags = true,
			.flags = src->flags,
		};
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_submessage(stream, usb_comm_FunctionSlotEvent_fields, &tmp)) {
			return false;
		}
	}
	return true;
}

/* ---------------- Config scalar helpers (unchanged logic, just factored out) ---------------- */

static void fill_slot_config_scalars(const struct hw75_function_slot_config *src,
				     usb_comm_FunctionSlotConfig *dst)
{
	dst->has_slot_index = true;
	dst->slot_index = src->slot_index;
	dst->has_slot_type = true;
	dst->slot_type = (usb_comm_FunctionSlotType)src->slot_type;
	dst->has_action_code = true;
	dst->action_code = src->action_code;
	dst->has_arg0 = true;
	dst->arg0 = src->arg0;
	dst->has_arg1 = true;
	dst->arg1 = src->arg1;
	dst->has_arg2 = true;
	dst->arg2 = src->arg2;
	dst->has_flags = true;
	dst->flags = src->flags;
}

static void copy_slot_config_scalars(const usb_comm_FunctionSlotConfig *src,
				     struct hw75_function_slot_config *dst)
{
	dst->slot_index = (uint8_t)src->slot_index;
	dst->slot_type = (uint8_t)src->slot_type;
	dst->action_code = (uint16_t)src->action_code;
	dst->arg0 = (int16_t)src->arg0;
	dst->arg1 = (int16_t)src->arg1;
	dst->arg2 = (int16_t)src->arg2;
	dst->flags = (uint8_t)src->flags;
}

/* ---------------- handlers ---------------- */

static bool handle_function_slot_get_config(const usb_comm_MessageH2D *h2d,
					    usb_comm_MessageD2H *d2h, const void *bytes,
					    uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	uint8_t slot_index = 0U;
	if (h2d->which_payload == usb_comm_MessageH2D_function_slot_request_tag &&
	    h2d->payload.function_slot_request.has_slot_index) {
		slot_index = (uint8_t)h2d->payload.function_slot_request.slot_index;
	}

	if (hw75_function_slot_get_config(slot_index, &g_slot_cfg_encode) != 0) {
		return false;
	}

	usb_comm_FunctionSlotConfig *res = &d2h->payload.function_slot_config;
	fill_slot_config_scalars(&g_slot_cfg_encode, res);
	res->macro_steps_packed.funcs.encode = encode_macro_steps;
	res->macro_steps_packed.arg = &g_slot_cfg_encode;
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_FUNCTION_SLOT_GET_CONFIG,
			usb_comm_MessageD2H_function_slot_config_tag,
			handle_function_slot_get_config);

static bool handle_function_slot_set_config(const usb_comm_MessageH2D *h2d,
					    usb_comm_MessageD2H *d2h, const void *bytes,
					    uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	/* Scalars already decoded into h2d->payload; macro_steps were piped
	 * into g_slot_cfg_decode by decode_macro_step() during pb_decode.
	 */
	copy_slot_config_scalars(&h2d->payload.function_slot_config, &g_slot_cfg_decode);

	if (hw75_function_slot_set_config(&g_slot_cfg_decode) != 0) {
		return false;
	}

	/* Echo the freshly-stored slot back via the GET path. */
	if (hw75_function_slot_get_config(g_slot_cfg_decode.slot_index, &g_slot_cfg_encode) != 0) {
		return false;
	}
	usb_comm_FunctionSlotConfig *res = &d2h->payload.function_slot_config;
	fill_slot_config_scalars(&g_slot_cfg_encode, res);
	res->macro_steps_packed.funcs.encode = encode_macro_steps;
	res->macro_steps_packed.arg = &g_slot_cfg_encode;
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_FUNCTION_SLOT_SET_CONFIG,
			usb_comm_MessageD2H_function_slot_config_tag,
			handle_function_slot_set_config);

static bool handle_function_slot_get_caps(const usb_comm_MessageH2D *h2d,
					  usb_comm_MessageD2H *d2h, const void *bytes,
					  uint32_t bytes_len)
{
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	if (hw75_function_slot_get_caps(&g_slot_caps) != 0) {
		return false;
	}

	usb_comm_FunctionSlotCaps *res = &d2h->payload.function_slot_caps;
	res->has_slot_count = true;
	res->slot_count = g_slot_caps.slot_count;
	res->has_max_macro_steps = true;
	res->max_macro_steps = g_slot_caps.max_macro_steps;
	res->supported_hid_presets.funcs.encode = encode_hid_presets;
	res->supported_hid_presets.arg = &g_slot_caps;
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_FUNCTION_SLOT_GET_FIRMWARE_CAPS,
			usb_comm_MessageD2H_function_slot_caps_tag,
			handle_function_slot_get_caps);

static bool handle_function_slot_get_events(const usb_comm_MessageH2D *h2d,
					    usb_comm_MessageD2H *d2h, const void *bytes,
					    uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	uint16_t after_seq = 0U;
	uint8_t max_count = HW75_FUNCTION_SLOT_EVENT_BATCH_MAX;

	if (h2d->which_payload == usb_comm_MessageH2D_function_slot_request_tag) {
		if (h2d->payload.function_slot_request.has_after_seq) {
			after_seq = (uint16_t)h2d->payload.function_slot_request.after_seq;
		}
		if (h2d->payload.function_slot_request.has_max_count) {
			max_count = (uint8_t)h2d->payload.function_slot_request.max_count;
		}
	}

	if (hw75_function_slot_fetch_events(after_seq, max_count, &g_slot_events) != 0) {
		return false;
	}

	usb_comm_FunctionSlotEvents *res = &d2h->payload.function_slot_events;
	res->has_dropped_count = true;
	res->dropped_count = g_slot_events.dropped_count;
	res->has_oldest_seq = true;
	res->oldest_seq = g_slot_events.oldest_seq;
	res->has_newest_seq = true;
	res->newest_seq = g_slot_events.newest_seq;
	res->events.funcs.encode = encode_events;
	res->events.arg = &g_slot_events;
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_FUNCTION_SLOT_TRIGGER_EVENT_GET,
			usb_comm_MessageD2H_function_slot_events_tag,
			handle_function_slot_get_events);

/* ---------------- prepare_decode (called from usb_comm_proto.c) ---------------- */

void usb_comm_function_slot_prepare_decode(usb_comm_FunctionSlotConfig *cfg)
{
	g_slot_cfg_decode = (struct hw75_function_slot_config){0};
	cfg->macro_steps_packed.funcs.decode = decode_macro_step;
	cfg->macro_steps_packed.arg = &g_slot_cfg_decode;
}
