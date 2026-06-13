/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/sys/util.h>

#include "handler.h"
#include "usb_comm.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include <app/touchbar.h>

/*
 * TouchbarConfig would be ~340 B if every nested submessage and mask array
 * were inlined. With that many static bytes per oneof slot, the keyboard's
 * 20 KB SRAM has no runtime stack margin left. Every "fat" field below is
 * therefore marked FT_CALLBACK in usb_comm.keyboard.options, so the C
 * struct only keeps an 8-byte pb_callback_t per field. The real data lives
 * in this file's single g_tb view and is produced/consumed through the
 * encode and decode callbacks.
 *
 * GET_CONFIG and SET_CONFIG both go through the same view: GET fills it
 * from the driver and lets nanopb call the encoders; SET lets the decoders
 * populate the view, then commits it back to the driver. usb_comm is
 * single-threaded so the shared state is safe.
 */

/* Bitmap tracking which optional submessage fields arrived during decode. */
#define TB_RX_PAN               BIT(0)
#define TB_RX_APP_SWITCH        BIT(1)
#define TB_RX_DESKTOP_SWITCH    BIT(2)
#define TB_RX_PAN_IND           BIT(3)
#define TB_RX_APP_IND           BIT(4)
#define TB_RX_DESKTOP_IND       BIT(5)
#define TB_RX_TOUCH_MASKS       BIT(6)
#define TB_RX_ENTRY_MASKS       BIT(7)

static struct {
	struct hw75_touchbar_config_view view;
	uint32_t received;
	uint8_t touch_mask_count;
	uint8_t entry_mask_count;
} g_tb;

/* ---------------- Submessage encoders (called once each) ---------------- */

static bool encode_pan(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct hw75_touchbar_pan_config *src = *arg;
	usb_comm_TouchbarPanConfig tmp = usb_comm_TouchbarPanConfig_init_zero;
	tmp.has_activation_ms = true;
	tmp.activation_ms = src->activation_ms;
	tmp.has_release_grace_ms = true;
	tmp.release_grace_ms = src->release_grace_ms;
	tmp.has_poll_interval_ms = true;
	tmp.poll_interval_ms = src->poll_interval_ms;
	tmp.has_interval_ms = true;
	tmp.interval_ms = src->interval_ms;
	tmp.has_deadzone = true;
	tmp.deadzone = src->deadzone;
	tmp.has_position_scale = true;
	tmp.position_scale = src->position_scale;
	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}
	return pb_encode_submessage(stream, usb_comm_TouchbarPanConfig_fields, &tmp);
}

static bool encode_app_switch(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct hw75_touchbar_app_switch_config *src = *arg;
	usb_comm_TouchbarAppSwitchConfig tmp = usb_comm_TouchbarAppSwitchConfig_init_zero;
	tmp.has_activation_ms = true;
	tmp.activation_ms = src->activation_ms;
	tmp.has_release_grace_ms = true;
	tmp.release_grace_ms = src->release_grace_ms;
	tmp.has_release_settle_ms = true;
	tmp.release_settle_ms = src->release_settle_ms;
	tmp.has_step_interval_ms = true;
	tmp.step_interval_ms = src->step_interval_ms;
	tmp.has_step_distance = true;
	tmp.step_distance = src->step_distance;
	tmp.has_edge_repeat_delay_ms = true;
	tmp.edge_repeat_delay_ms = src->edge_repeat_delay_ms;
	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}
	return pb_encode_submessage(stream, usb_comm_TouchbarAppSwitchConfig_fields, &tmp);
}

static bool encode_desktop_switch(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct hw75_touchbar_desktop_switch_config *src = *arg;
	usb_comm_TouchbarDesktopSwitchConfig tmp = usb_comm_TouchbarDesktopSwitchConfig_init_zero;
	tmp.has_activation_ms = true;
	tmp.activation_ms = src->activation_ms;
	tmp.has_release_grace_ms = true;
	tmp.release_grace_ms = src->release_grace_ms;
	tmp.has_hold_ms = true;
	tmp.hold_ms = src->hold_ms;
	tmp.has_step_interval_ms = true;
	tmp.step_interval_ms = src->step_interval_ms;
	tmp.has_step_distance = true;
	tmp.step_distance = src->step_distance;
	tmp.has_edge_repeat_delay_ms = true;
	tmp.edge_repeat_delay_ms = src->edge_repeat_delay_ms;
	tmp.has_swipe_distance = true;
	tmp.swipe_distance = src->swipe_distance;
	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}
	return pb_encode_submessage(stream, usb_comm_TouchbarDesktopSwitchConfig_fields, &tmp);
}

static bool encode_indicator(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct hw75_touchbar_mode_indicator *src = *arg;
	usb_comm_TouchbarModeIndicator tmp = usb_comm_TouchbarModeIndicator_init_zero;
	tmp.has_color_rgb = true;
	tmp.color_rgb = ((uint32_t)src->red << 16) | ((uint32_t)src->green << 8) |
			(uint32_t)src->blue;
	tmp.has_duration_ms = true;
	tmp.duration_ms = src->duration_ms;
	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}
	return pb_encode_submessage(stream, usb_comm_TouchbarModeIndicator_fields, &tmp);
}

/* ---------------- Submessage decoders (called once each) ---------------- */

static bool decode_pan(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	struct hw75_touchbar_pan_config *dst = *arg;
	usb_comm_TouchbarPanConfig tmp = usb_comm_TouchbarPanConfig_init_zero;
	if (!pb_decode(stream, usb_comm_TouchbarPanConfig_fields, &tmp)) {
		return false;
	}
	if (tmp.has_activation_ms) {
		dst->activation_ms = (uint16_t)tmp.activation_ms;
	}
	if (tmp.has_release_grace_ms) {
		dst->release_grace_ms = (uint16_t)tmp.release_grace_ms;
	}
	if (tmp.has_poll_interval_ms) {
		dst->poll_interval_ms = (uint16_t)tmp.poll_interval_ms;
	}
	if (tmp.has_interval_ms) {
		dst->interval_ms = (uint16_t)tmp.interval_ms;
	}
	if (tmp.has_deadzone) {
		dst->deadzone = (uint16_t)tmp.deadzone;
	}
	if (tmp.has_position_scale) {
		dst->position_scale = (uint16_t)tmp.position_scale;
	}
	g_tb.received |= TB_RX_PAN;
	return true;
}

static bool decode_app_switch(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	struct hw75_touchbar_app_switch_config *dst = *arg;
	usb_comm_TouchbarAppSwitchConfig tmp = usb_comm_TouchbarAppSwitchConfig_init_zero;
	if (!pb_decode(stream, usb_comm_TouchbarAppSwitchConfig_fields, &tmp)) {
		return false;
	}
	if (tmp.has_activation_ms) {
		dst->activation_ms = (uint16_t)tmp.activation_ms;
	}
	if (tmp.has_release_grace_ms) {
		dst->release_grace_ms = (uint16_t)tmp.release_grace_ms;
	}
	if (tmp.has_release_settle_ms) {
		dst->release_settle_ms = (uint16_t)tmp.release_settle_ms;
	}
	if (tmp.has_step_interval_ms) {
		dst->step_interval_ms = (uint16_t)tmp.step_interval_ms;
	}
	if (tmp.has_step_distance) {
		dst->step_distance = (uint16_t)tmp.step_distance;
	}
	if (tmp.has_edge_repeat_delay_ms) {
		dst->edge_repeat_delay_ms = (uint16_t)tmp.edge_repeat_delay_ms;
	}
	g_tb.received |= TB_RX_APP_SWITCH;
	return true;
}

static bool decode_desktop_switch(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	struct hw75_touchbar_desktop_switch_config *dst = *arg;
	usb_comm_TouchbarDesktopSwitchConfig tmp = usb_comm_TouchbarDesktopSwitchConfig_init_zero;
	if (!pb_decode(stream, usb_comm_TouchbarDesktopSwitchConfig_fields, &tmp)) {
		return false;
	}
	if (tmp.has_activation_ms) {
		dst->activation_ms = (uint16_t)tmp.activation_ms;
	}
	if (tmp.has_release_grace_ms) {
		dst->release_grace_ms = (uint16_t)tmp.release_grace_ms;
	}
	if (tmp.has_hold_ms) {
		dst->hold_ms = (uint16_t)tmp.hold_ms;
	}
	if (tmp.has_step_interval_ms) {
		dst->step_interval_ms = (uint16_t)tmp.step_interval_ms;
	}
	if (tmp.has_step_distance) {
		dst->step_distance = (uint16_t)tmp.step_distance;
	}
	if (tmp.has_edge_repeat_delay_ms) {
		dst->edge_repeat_delay_ms = (uint16_t)tmp.edge_repeat_delay_ms;
	}
	if (tmp.has_swipe_distance) {
		dst->swipe_distance = (uint16_t)tmp.swipe_distance;
	}
	g_tb.received |= TB_RX_DESKTOP_SWITCH;
	return true;
}

static bool decode_indicator_into(pb_istream_t *stream, struct hw75_touchbar_mode_indicator *dst)
{
	usb_comm_TouchbarModeIndicator tmp = usb_comm_TouchbarModeIndicator_init_zero;
	if (!pb_decode(stream, usb_comm_TouchbarModeIndicator_fields, &tmp)) {
		return false;
	}
	if (tmp.has_color_rgb) {
		dst->red = (uint8_t)((tmp.color_rgb >> 16) & 0xFFU);
		dst->green = (uint8_t)((tmp.color_rgb >> 8) & 0xFFU);
		dst->blue = (uint8_t)(tmp.color_rgb & 0xFFU);
	}
	if (tmp.has_duration_ms) {
		dst->duration_ms = (uint16_t)tmp.duration_ms;
	}
	return true;
}

static bool decode_pan_indicator(pb_istream_t *s, const pb_field_t *f, void **arg)
{
	ARG_UNUSED(f);
	if (!decode_indicator_into(s, *arg)) {
		return false;
	}
	g_tb.received |= TB_RX_PAN_IND;
	return true;
}

static bool decode_app_indicator(pb_istream_t *s, const pb_field_t *f, void **arg)
{
	ARG_UNUSED(f);
	if (!decode_indicator_into(s, *arg)) {
		return false;
	}
	g_tb.received |= TB_RX_APP_IND;
	return true;
}

static bool decode_desktop_indicator(pb_istream_t *s, const pb_field_t *f, void **arg)
{
	ARG_UNUSED(f);
	if (!decode_indicator_into(s, *arg)) {
		return false;
	}
	g_tb.received |= TB_RX_DESKTOP_IND;
	return true;
}

/* ---------------- Repeated uint32 mask encode/decode ---------------- */

struct mask_encode_ctx {
	const uint8_t *values;
	uint8_t count;
};

static bool encode_masks(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct mask_encode_ctx *ctx = *arg;
	for (uint8_t i = 0; i < ctx->count; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_varint(stream, ctx->values[i])) {
			return false;
		}
	}
	return true;
}

static bool decode_touch_mask(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	ARG_UNUSED(arg);
	uint64_t value;
	if (!pb_decode_varint(stream, &value)) {
		return false;
	}
	if (g_tb.touch_mask_count < HW75_TOUCHBAR_MAX_SEGMENT_COUNT) {
		g_tb.view.segment_touch_masks[g_tb.touch_mask_count++] = (uint8_t)value;
	}
	g_tb.received |= TB_RX_TOUCH_MASKS;
	return true;
}

static bool decode_entry_mask(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	ARG_UNUSED(arg);
	uint64_t value;
	if (!pb_decode_varint(stream, &value)) {
		return false;
	}
	if (g_tb.entry_mask_count < HW75_TOUCHBAR_MAX_SEGMENT_COUNT) {
		g_tb.view.segment_entry_masks[g_tb.entry_mask_count++] = (uint8_t)value;
	}
	g_tb.received |= TB_RX_ENTRY_MASKS;
	return true;
}

/* ---------------- Logical-map packing helpers ---------------- */

static uint32_t pack_touchbar_map0(const struct hw75_touchbar_config_view *view)
{
	return ((uint32_t)view->logical_rows[0] & 0xFU) |
	       (((uint32_t)view->logical_cols[0] & 0xFU) << 4) |
	       (((uint32_t)view->logical_rows[1] & 0xFU) << 8) |
	       (((uint32_t)view->logical_cols[1] & 0xFU) << 12) |
	       (((uint32_t)view->logical_rows[2] & 0xFU) << 16) |
	       (((uint32_t)view->logical_cols[2] & 0xFU) << 20) |
	       (((uint32_t)view->logical_rows[3] & 0xFU) << 24) |
	       (((uint32_t)view->logical_cols[3] & 0xFU) << 28);
}

static uint32_t pack_touchbar_map1(const struct hw75_touchbar_config_view *view)
{
	return ((uint32_t)view->logical_rows[4] & 0xFU) |
	       (((uint32_t)view->logical_cols[4] & 0xFU) << 4) |
	       (((uint32_t)view->logical_rows[5] & 0xFU) << 8) |
	       (((uint32_t)view->logical_cols[5] & 0xFU) << 12);
}

/* ---------------- GET_CONFIG handler ---------------- */

static bool handle_touchbar_get_config(const usb_comm_MessageH2D *h2d,
				       usb_comm_MessageD2H *d2h, const void *bytes,
				       uint32_t bytes_len)
{
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	if (touchbar_get_config_view(&g_tb.view) != 0) {
		return false;
	}

	usb_comm_TouchbarConfig *res = &d2h->payload.touchbar_config;

	res->has_mode = true;
	res->mode = (usb_comm_TouchbarMode)g_tb.view.mode;
	res->has_logical_point_count = true;
	res->logical_point_count = g_tb.view.logical_point_count;
	res->has_segment_count = true;
	res->segment_count = g_tb.view.segment_count;
	res->has_shared_point_count = true;
	res->shared_point_count = g_tb.view.shared_point_count;
	if (g_tb.view.segment_count > 0U) {
		res->has_left_touch_mask = true;
		res->left_touch_mask = g_tb.view.segment_touch_masks[0];
		res->has_left_entry_mask = true;
		res->left_entry_mask = g_tb.view.segment_entry_masks[0];
	}
	if (g_tb.view.segment_count > 1U) {
		res->has_right_touch_mask = true;
		res->right_touch_mask = g_tb.view.segment_touch_masks[1];
		res->has_right_entry_mask = true;
		res->right_entry_mask = g_tb.view.segment_entry_masks[1];
	}
	res->has_logical_map_packed0 = true;
	res->logical_map_packed0 = pack_touchbar_map0(&g_tb.view);
	res->has_logical_map_packed1 = true;
	res->logical_map_packed1 = pack_touchbar_map1(&g_tb.view);
	res->has_mode_indicator_enabled = true;
	res->mode_indicator_enabled = g_tb.view.mode_indicator_enabled;

	/* Nested submessages and repeated masks flow through callbacks so the
	 * generated TouchbarConfig struct stays small.
	 */
	res->pan.funcs.encode = encode_pan;
	res->pan.arg = &g_tb.view.pan;
	res->app_switch.funcs.encode = encode_app_switch;
	res->app_switch.arg = &g_tb.view.app_switch;
	res->desktop_switch.funcs.encode = encode_desktop_switch;
	res->desktop_switch.arg = &g_tb.view.desktop_switch;

	res->pan_indicator.funcs.encode = encode_indicator;
	res->pan_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_PAN];
	res->app_indicator.funcs.encode = encode_indicator;
	res->app_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_APP_SWITCH];
	res->desktop_indicator.funcs.encode = encode_indicator;
	res->desktop_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_DESKTOP_SWITCH];

	static struct mask_encode_ctx touch_ctx;
	static struct mask_encode_ctx entry_ctx;
	touch_ctx.values = g_tb.view.segment_touch_masks;
	touch_ctx.count = g_tb.view.segment_count;
	entry_ctx.values = g_tb.view.segment_entry_masks;
	entry_ctx.count = g_tb.view.segment_count;
	res->segment_touch_masks.funcs.encode = encode_masks;
	res->segment_touch_masks.arg = &touch_ctx;
	res->segment_entry_masks.funcs.encode = encode_masks;
	res->segment_entry_masks.arg = &entry_ctx;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_TOUCHBAR_GET_CONFIG,
			usb_comm_MessageD2H_touchbar_config_tag,
			handle_touchbar_get_config);

/* ---------------- SET_CONFIG handler ---------------- */

static bool handle_touchbar_set_config(const usb_comm_MessageH2D *h2d,
				       usb_comm_MessageD2H *d2h, const void *bytes,
				       uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	/* g_tb.view has already been seeded from the driver by
	 * usb_comm_touchbar_prepare_decode() and then mutated in place by the
	 * per-field decoders while pb_decode was running. Only the scalar
	 * fields and the legacy left/right masks still need to be applied
	 * here: they stayed inside h2d->payload.touchbar_config.
	 */
	const usb_comm_TouchbarConfig *req = &h2d->payload.touchbar_config;

	if (req->has_mode) {
		g_tb.view.mode = (enum hw75_touchbar_mode)req->mode;
	}
	if (req->has_segment_count) {
		g_tb.view.segment_count = (uint8_t)req->segment_count;
	}
	if ((g_tb.received & (TB_RX_TOUCH_MASKS | TB_RX_ENTRY_MASKS)) ==
	    (TB_RX_TOUCH_MASKS | TB_RX_ENTRY_MASKS)) {
		/* Both mask arrays present: their lengths must agree, otherwise the
		 * chosen segment_count would be arbitrary (last-wins) and pair fresh
		 * masks against stale seeded slots. The official host always sends
		 * equal lengths; reject a mismatched pair rather than guess.
		 */
		if (g_tb.touch_mask_count != g_tb.entry_mask_count) {
			return false;
		}
		g_tb.view.segment_count = g_tb.touch_mask_count;
	} else if (g_tb.received & TB_RX_TOUCH_MASKS) {
		g_tb.view.segment_count = g_tb.touch_mask_count;
	} else if (g_tb.received & TB_RX_ENTRY_MASKS) {
		g_tb.view.segment_count = g_tb.entry_mask_count;
	}
	if ((g_tb.received & (TB_RX_TOUCH_MASKS | TB_RX_ENTRY_MASKS)) == 0U) {
		/* No new masks: fall back to legacy two-segment fields so older hosts
		 * can still poke the touchbar config — but only force 2 segments when
		 * the host didn't explicitly send segment_count, else a mode-only SET
		 * would truncate a >2-segment config seeded from the driver.
		 */
		if (!req->has_segment_count) {
			g_tb.view.segment_count = 2U;
		}
		if (req->has_left_touch_mask) {
			g_tb.view.segment_touch_masks[0] = (uint8_t)req->left_touch_mask;
		}
		if (req->has_left_entry_mask) {
			g_tb.view.segment_entry_masks[0] = (uint8_t)req->left_entry_mask;
		}
		if (req->has_right_touch_mask) {
			g_tb.view.segment_touch_masks[1] = (uint8_t)req->right_touch_mask;
		}
		if (req->has_right_entry_mask) {
			g_tb.view.segment_entry_masks[1] = (uint8_t)req->right_entry_mask;
		}
	}
	if (req->has_mode_indicator_enabled) {
		g_tb.view.mode_indicator_enabled = req->mode_indicator_enabled;
	}

	if (touchbar_set_config_view(&g_tb.view) != 0) {
		return false;
	}

	return handle_touchbar_get_config(h2d, d2h, NULL, 0U);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_TOUCHBAR_SET_CONFIG,
			usb_comm_MessageD2H_touchbar_config_tag,
			handle_touchbar_set_config);

/* ---------------- prepare_decode (called from usb_comm_proto.c) ---------------- */

void usb_comm_touchbar_prepare_decode(usb_comm_TouchbarConfig *cfg)
{
	/* Pre-seed the view with the current driver config so a partial SET
	 * (e.g. just a mode change) inherits everything else unchanged. The
	 * decoders will overwrite only the fields that actually appear on
	 * the wire; received tracks which ones did.
	 */
	(void)touchbar_get_config_view(&g_tb.view);
	g_tb.received = 0U;
	g_tb.touch_mask_count = 0U;
	g_tb.entry_mask_count = 0U;

	cfg->pan.funcs.decode = decode_pan;
	cfg->pan.arg = &g_tb.view.pan;
	cfg->app_switch.funcs.decode = decode_app_switch;
	cfg->app_switch.arg = &g_tb.view.app_switch;
	cfg->desktop_switch.funcs.decode = decode_desktop_switch;
	cfg->desktop_switch.arg = &g_tb.view.desktop_switch;

	cfg->pan_indicator.funcs.decode = decode_pan_indicator;
	cfg->pan_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_PAN];
	cfg->app_indicator.funcs.decode = decode_app_indicator;
	cfg->app_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_APP_SWITCH];
	cfg->desktop_indicator.funcs.decode = decode_desktop_indicator;
	cfg->desktop_indicator.arg = &g_tb.view.mode_indicators[HW75_TOUCHBAR_MODE_DESKTOP_SWITCH];

	cfg->segment_touch_masks.funcs.decode = decode_touch_mask;
	cfg->segment_entry_masks.funcs.decode = decode_entry_mask;
}
