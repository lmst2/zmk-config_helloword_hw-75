/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "handler.h"
#include "usb_comm.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <eink_mode.h>

/*
 * EinkModeConfig.modes is a callback-style repeated field (see usb_comm.proto
 * comments). We stage the view in a file-scope static on both the encode and
 * decode paths so the callback has stable memory to read/write from across the
 * pb_encode / pb_decode calls.
 */
static struct eink_mode_view g_encode_view;
static struct eink_mode_view g_decode_view;

static bool encode_modes(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct eink_mode_view *view = *arg;
	for (uint8_t i = 0; i < view->count; i++) {
		const struct eink_mode_entry *src = &view->modes[i];
		usb_comm_EinkModeEntry dst = usb_comm_EinkModeEntry_init_zero;
		dst.id = src->id;
		dst.type = (usb_comm_EinkModeType)src->type;
		dst.has_label = true;
		strncpy(dst.label, src->label, sizeof(dst.label) - 1);
		dst.label[sizeof(dst.label) - 1] = '\0';
		dst.has_refresh_interval_s = true;
		dst.refresh_interval_s = src->refresh_interval_s;
		dst.has_frame_count = true;
		dst.frame_count = src->frame_count;

		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}
		if (!pb_encode_submessage(stream, usb_comm_EinkModeEntry_fields, &dst)) {
			return false;
		}
	}
	return true;
}

static bool decode_mode_entry(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	struct eink_mode_view *view = *arg;
	if (view->count >= EINK_MODE_CAPACITY) {
		/* Silently drop extras rather than aborting the whole decode. */
		return true;
	}

	usb_comm_EinkModeEntry src = usb_comm_EinkModeEntry_init_zero;
	if (!pb_decode(stream, usb_comm_EinkModeEntry_fields, &src)) {
		return false;
	}

	struct eink_mode_entry *dst = &view->modes[view->count++];
	memset(dst, 0, sizeof(*dst));
	dst->id = (uint8_t)src.id;
	dst->type = (enum eink_mode_type)src.type;
	dst->refresh_interval_s = src.has_refresh_interval_s ? src.refresh_interval_s : 0;
	dst->frame_count = src.has_frame_count ? (uint8_t)src.frame_count : 0;
	if (src.has_label) {
		strncpy(dst->label, src.label, EINK_MODE_LABEL_LEN - 1);
		dst->label[EINK_MODE_LABEL_LEN - 1] = '\0';
	}
	return true;
}

static bool handle_eink_get_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	eink_mode_get_view(&g_encode_view);

	usb_comm_EinkModeConfig *res = &d2h->payload.eink_mode_config;
	res->modes.funcs.encode = encode_modes;
	res->modes.arg = &g_encode_view;
	res->has_active_index = true;
	res->active_index = g_encode_view.active_index;
	res->has_capacity = true;
	res->capacity = EINK_MODE_CAPACITY;
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_GET_CONFIG,
			usb_comm_MessageD2H_eink_mode_config_tag, handle_eink_get_config);

static bool handle_eink_set_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_EinkModeConfig *req = &h2d->payload.eink_mode_config;
	/*
	 * h2d was already decoded before the handler was dispatched. The decode
	 * callback for modes populated g_decode_view; we only need to pull the
	 * active index + commit the view to the mode engine.
	 */
	uint8_t active = req->has_active_index ? (uint8_t)req->active_index : 0;
	int ret = eink_mode_set_config(g_decode_view.modes, g_decode_view.count, active);
	if (ret != 0) {
		LOG_ERR("eink_mode_set_config failed: %d", ret);
		return false;
	}

	return handle_eink_get_config(h2d, d2h, NULL, 0);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_SET_CONFIG,
			usb_comm_MessageD2H_eink_mode_config_tag, handle_eink_set_config);

static bool handle_eink_set_active(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	uint8_t index = (uint8_t)h2d->payload.eink_active.active_index;
	int ret = eink_mode_set_active(index);
	if (ret != 0) {
		LOG_ERR("eink_mode_set_active(%u) failed: %d", index, ret);
		return false;
	}
	return handle_eink_get_config(h2d, d2h, NULL, 0);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_SET_ACTIVE,
			usb_comm_MessageD2H_eink_mode_config_tag, handle_eink_set_active);

static bool handle_eink_push_frame(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);

	const usb_comm_EinkFrame *req = &h2d->payload.eink_frame;
	int ret = eink_mode_push_frame((uint8_t)req->mode_id, (uint8_t)req->frame_index, bytes,
				       bytes_len);
	if (ret != 0) {
		LOG_ERR("eink_mode_push_frame(%u,%u) failed: %d", req->mode_id, req->frame_index,
			ret);
		return false;
	}
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_PUSH_FRAME, usb_comm_MessageD2H_nop_tag,
			handle_eink_push_frame);

static bool handle_eink_push_clock(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_EinkClock *req = &h2d->payload.eink_clock;
	eink_mode_push_clock((uint8_t)req->hour, (uint8_t)req->minute,
			     req->has_day ? (uint8_t)req->day : 0,
			     req->has_month ? (uint8_t)req->month : 0,
			     req->has_weekday ? (uint8_t)req->weekday : 0,
			     req->has_year ? (uint16_t)req->year : 0);
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_PUSH_CLOCK, usb_comm_MessageD2H_nop_tag,
			handle_eink_push_clock);

static bool handle_eink_push_weather(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				     const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_EinkWeather *req = &h2d->payload.eink_weather;
	eink_mode_push_weather((int16_t)req->temp_deci_c, (uint8_t)req->icon,
			       req->has_city ? req->city : NULL);
	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_EINK_PUSH_WEATHER, usb_comm_MessageD2H_nop_tag,
			handle_eink_push_weather);

/*
 * Wire up the decode callback before pb_decode runs, matching the pattern
 * used by handler_eink.c for EinkImage.bits in usb_comm_proto.c.
 */
void usb_comm_eink_mode_prepare_decode(usb_comm_EinkModeConfig *cfg)
{
	g_decode_view.count = 0;
	cfg->modes.funcs.decode = decode_mode_entry;
	cfg->modes.arg = &g_decode_view;
}
