/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include <arm_math.h>

#include "handler.h"
#include "usb_comm.pb.h"

#include <zephyr/device.h>
#include <knob/drivers/knob.h>
#include <knob/drivers/motor.h>

#include <knob_app.h>

#include <pb_encode.h>

#define KNOB_NODE DT_ALIAS(knob)
#define MOTOR_NODE DT_PHANDLE(KNOB_NODE, motor)

#define DEG(deg) (deg / 360.0f * (PI * 2.0f))

#define PROFILE_TORQUE_LIMIT(node) (float)DT_PROP_OR(node, torque_limit_mv, 0) / 1000.0f,

static const struct device *knob = DEVICE_DT_GET(KNOB_NODE);
static const struct device *motor = DEVICE_DT_GET(MOTOR_NODE);

static const float default_torque_limits[] = { DT_FOREACH_CHILD(KNOB_NODE, PROFILE_TORQUE_LIMIT) };

static struct motor_state state = {};

static bool handle_motor_get_state(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	usb_comm_MotorState *res = &d2h->payload.motor_state;

	if (!motor) {
		return false;
	}

	motor_inspect(motor, &state);

	res->timestamp = state.timestamp;
	res->control_mode = (usb_comm_MotorState_ControlMode)state.control_mode;
	res->current_angle = state.current_angle;
	res->current_velocity = state.current_velocity;
	res->target_angle = state.target_angle;
	res->target_velocity = state.target_velocity;
	res->target_voltage = state.target_voltage;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_MOTOR_GET_STATE, usb_comm_MessageD2H_motor_state_tag,
			handle_motor_get_state);

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
	const struct knob_pref *prefs;
	const char **names;
	int layers = knob_app_get_prefs(&prefs, &names);
	if (!layers) {
		return false;
	}

	usb_comm_KnobConfig_Pref pref = usb_comm_KnobConfig_Pref_init_zero;
	for (int i = 0; i < layers; i++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}

		pref.layer_id = i;
		pref.layer_name.funcs.encode = write_string;
		pref.layer_name.arg = (void *)names[i];
		pref.active = prefs[i].active;
		pref.mode = (usb_comm_KnobConfig_Mode)prefs[i].mode;
		pref.has_mode = true;
		pref.ppr = prefs[i].ppr;
		pref.has_ppr = true;
		pref.torque_limit = prefs[i].torque_limit;
		pref.has_torque_limit = true;

		if (!pb_encode_submessage(stream, usb_comm_KnobConfig_Pref_fields, &pref)) {
			return false;
		}
	}

	return true;
}

static bool handle_knob_get_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	usb_comm_KnobConfig *res = &d2h->payload.knob_config;

	if (!knob) {
		return false;
	}

	res->demo = knob_app_get_demo();
	res->mode = (usb_comm_KnobConfig_Mode)knob_get_mode(knob);
	res->prefs.funcs.encode = write_prefs;

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_GET_CONFIG, usb_comm_MessageD2H_knob_config_tag,
			handle_knob_get_config);

static bool handle_knob_set_config(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				   const void *bytes, uint32_t bytes_len)
{
	const usb_comm_KnobConfig *req = &h2d->payload.knob_config;

	if (!knob) {
		return false;
	}

	if (req->demo) {
		knob_app_set_demo(true);
		knob_set_mode(knob, (enum knob_mode)req->mode);
		if (req->mode == usb_comm_KnobConfig_Mode_DAMPED) {
			knob_set_position_limit(knob, DEG(110.0f), DEG(250.0f));
		} else {
			knob_set_position_limit(knob, 0.0f, 0.0f);
		}
	} else {
		knob_set_position_limit(knob, 0.0f, 0.0f);
		knob_app_set_demo(false);
	}

	return handle_knob_get_config(h2d, d2h, NULL, 0);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_SET_CONFIG, usb_comm_MessageD2H_knob_config_tag,
			handle_knob_set_config);

static bool handle_knob_update_pref(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				    const void *bytes, uint32_t bytes_len)
{
	const usb_comm_KnobConfig_Pref *req = &h2d->payload.knob_pref;
	usb_comm_KnobConfig_Pref *res = &d2h->payload.knob_pref;
	const struct knob_pref *pref;

	if (req->active) {
		pref = knob_app_get_pref(req->layer_id);
		if (pref == NULL) {
			return false;
		}

		struct knob_pref next;
		memcpy(&next, pref, sizeof(next));

		next.active = req->active;
		if (req->has_mode) {
			next.mode = (enum knob_mode)req->mode;
			if (req->mode > 0 && req->mode < ARRAY_SIZE(default_torque_limits)) {
				next.torque_limit = default_torque_limits[req->mode];
			}
		}
		if (req->has_ppr) {
			next.ppr = req->ppr;
		}
		if (req->has_torque_limit) {
			next.torque_limit = req->torque_limit;
		}

		knob_app_set_pref(req->layer_id, &next);
	} else {
		knob_app_reset_pref(req->layer_id);
	}

	{
		pref = knob_app_get_pref(req->layer_id);
		if (pref == NULL) {
			return false;
		}

		res->layer_id = req->layer_id;
		res->active = pref->active;
		res->mode = (usb_comm_KnobConfig_Mode)pref->mode;
		res->has_mode = true;
		res->ppr = pref->ppr;
		res->has_ppr = true;
		res->torque_limit = pref->torque_limit;
		res->has_torque_limit = true;
	}

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_UPDATE_PREF, usb_comm_MessageD2H_knob_pref_tag,
			handle_knob_update_pref);

static bool fill_calibration_response(usb_comm_KnobCalibration *res)
{
	if (!knob || !motor) {
		return false;
	}

	/*
	 * KnobCalibration.zero_offset is the user-level knob position offset
	 * (radians) that spring / damped profiles use as their resting centre.
	 * KnobCalibration.direction surfaces the FOC rotation direction detected
	 * by auto-cal for diagnostics, but the UI does not control it anymore.
	 */
	float user_offset = knob_get_position_offset(knob);
	enum motor_direction dir = UNKNOWN;
	float foc_offset = 0.0f;
	motor_calibrate_get(motor, &foc_offset, &dir);

	res->has_zero_offset = true;
	res->zero_offset = user_offset;
	res->has_direction = true;
	res->direction = (int32_t)dir;
	res->has_calibrated = true;
	res->calibrated = motor_is_calibrated(motor);
	res->has_reset_auto = false;
	return true;
}

static bool handle_knob_get_calibration(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
					const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(h2d);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	usb_comm_KnobCalibration *res = &d2h->payload.knob_calibration;
	return fill_calibration_response(res);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_GET_CALIBRATION,
			usb_comm_MessageD2H_knob_calibration_tag, handle_knob_get_calibration);

static bool handle_knob_set_calibration(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
					const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_KnobCalibration *req = &h2d->payload.knob_calibration;

	if (req->has_reset_auto && req->reset_auto) {
		knob_app_recalibrate_auto();
	} else if (req->has_zero_offset) {
		/* Only the user-level knob position offset is mutable. We ignore
		 * has_direction because the FOC direction is owned by auto-cal.
		 */
		knob_app_set_calibration(req->zero_offset, 0);
	}

	usb_comm_KnobCalibration *res = &d2h->payload.knob_calibration;
	return fill_calibration_response(res);
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_SET_CALIBRATION,
			usb_comm_MessageD2H_knob_calibration_tag, handle_knob_set_calibration);

static bool handle_knob_pulse(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
			      const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_KnobPulse *req = &h2d->payload.knob_pulse;

	if (!knob) {
		return false;
	}

	uint8_t strength = req->has_strength ? (uint8_t)MIN(req->strength, 100U) : 50U;
	uint8_t count = req->has_count ? (uint8_t)CLAMP(req->count, 1U, 10U) : 1U;
	knob_pulse(knob, strength, count);

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_PULSE, usb_comm_MessageD2H_nop_tag, handle_knob_pulse);

static bool handle_knob_set_detents(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				    const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_KnobDetents *req = &h2d->payload.knob_detents;

	if (!knob) {
		return false;
	}

	uint8_t count = (uint8_t)CLAMP(req->count, 1U, 255U);
	uint8_t strength = req->has_strength ? (uint8_t)MIN(req->strength, 100U) : 50U;
	bool endstops = req->has_endstops && req->endstops;
	knob_set_detents(knob, count, strength, endstops);

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_SET_DETENTS, usb_comm_MessageD2H_nop_tag,
			handle_knob_set_detents);

static bool handle_knob_set_feel(const usb_comm_MessageH2D *h2d, usb_comm_MessageD2H *d2h,
				 const void *bytes, uint32_t bytes_len)
{
	ARG_UNUSED(d2h);
	ARG_UNUSED(bytes);
	ARG_UNUSED(bytes_len);

	const usb_comm_KnobFeel *req = &h2d->payload.knob_feel;

	if (!knob) {
		return false;
	}

	uint8_t mode = (uint8_t)MIN(req->mode, 7U);
	uint16_t ppr = req->has_ppr ? (uint16_t)req->ppr : 0U;
	uint8_t strength = req->has_strength ? (uint8_t)MIN(req->strength, 100U) : 50U;
	knob_set_feel(knob, mode, ppr, strength);

	return true;
}

USB_COMM_HANDLER_DEFINE(usb_comm_Action_KNOB_SET_FEEL, usb_comm_MessageD2H_nop_tag,
			handle_knob_set_feel);
