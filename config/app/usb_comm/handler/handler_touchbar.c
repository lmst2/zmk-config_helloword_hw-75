/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include "handler.h"
#include "usb_comm.pb.h"

#include <pb_encode.h>
#include <string.h>

#include <touchbar_app.h>

static bool write_string(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	char *str = *arg;
	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}
	return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

static bool write_prefs(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct touchbar_pref *prefs;
	const char **names;
	int layers = touchbar_app_get_prefs(&prefs, &names);
	if (!layers) {
		return false;
	}

	usb_comm_TouchbarConfig_Pref pref = usb_comm_TouchbarConfig_Pref_init_zero;
	for (int i = 0; i < layers; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}

		pref.layer_id = i;
		pref.layer_name.funcs.encode = write_string;
		pref.layer_name.arg = (void *)names[i];
		pref.active = prefs[i].active;
		pref.mode = (usb_comm_TouchbarConfig_Mode)prefs[i].mode;
		pref.has_mode = true;

		if (!pb_encode_submessage(stream, usb_comm_TouchbarConfig_Pref_fields, &pref)) {
			return false;
		}
	}

	return true;
}

static bool handle_touchbar_get_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				       const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	usb_comm_TouchbarConfig *res = &d2h->payload.touchbar_config;
	res->enable = touchbar_app_get_enable();
	res->default_mode = (usb_comm_TouchbarConfig_Mode)touchbar_app_get_default_mode();
	res->has_default_mode = true;
	res->mode = (usb_comm_TouchbarConfig_Mode)touchbar_app_get_mode();
	res->has_mode = true;
	res->prefs.funcs.encode = write_prefs;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_TOUCHBAR_GET_CONFIG,
			usb_comm_MessageD2H_touchbar_config_tag, handle_touchbar_get_config);

static bool handle_touchbar_set_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				       const void *bytes, uint32_t bytes_len)
{
	const usb_comm_TouchbarConfig *req = &h2d->payload.touchbar_config;

	touchbar_app_set_enable(req->enable);
	if (req->has_default_mode) {
		touchbar_app_set_default_mode((enum touchbar_mode)req->default_mode);
	} else if (req->has_mode) {
		touchbar_app_set_default_mode((enum touchbar_mode)req->mode);
	}

	return handle_touchbar_get_config(h2d, d2h, bytes, bytes_len);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_TOUCHBAR_SET_CONFIG,
			usb_comm_MessageD2H_touchbar_config_tag, handle_touchbar_set_config);

static bool handle_touchbar_update_pref(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
					const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_TouchbarConfig_Pref *req = &h2d->payload.touchbar_pref;
	usb_comm_TouchbarConfig_Pref *res = &d2h->payload.touchbar_pref;
	const struct touchbar_pref *pref;

	if (req->active) {
		pref = touchbar_app_get_pref(req->layer_id);
		if (pref == NULL) {
			return false;
		}

		struct touchbar_pref next;
		memcpy(&next, pref, sizeof(next));
		next.active = true;
		if (req->has_mode) {
			next.mode = (enum touchbar_mode)req->mode;
		}

		touchbar_app_set_pref(req->layer_id, &next);
	} else {
		touchbar_app_reset_pref(req->layer_id);
	}

	pref = touchbar_app_get_pref(req->layer_id);
	if (pref == NULL) {
		return false;
	}

	res->layer_id = req->layer_id;
	res->active = pref->active;
	res->mode = (usb_comm_TouchbarConfig_Mode)pref->mode;
	res->has_mode = true;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_TOUCHBAR_UPDATE_PREF,
			usb_comm_MessageD2H_touchbar_pref_tag, handle_touchbar_update_pref);
