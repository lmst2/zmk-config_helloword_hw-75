/*
 * Copyright (c) 2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_knob_profile_inertia

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <knob/math.h>
#include <knob/drivers/knob.h>
#include <knob/drivers/motor.h>
#include <knob/drivers/profile.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(knob_inertia, CONFIG_ZMK_LOG_LEVEL);

/*
 * The profile tick runs every 200 us (5 kHz), so the decay is a per-tick factor
 * very close to 1. ~0.9995 gives roughly a 1.5-2 s coast; closer to 1.0 coasts
 * longer, lower stops sooner. This is the knob "damping" the user tunes (web
 * config wiring is a follow-up); DEADBAND/STOP are in rad/s.
 */
#define KNOB_INERTIA_DEFAULT_DECAY 0.9995f
#define KNOB_INERTIA_DEADBAND 0.25f
#define KNOB_INERTIA_STOP 0.50f

static inline float absf(float x)
{
	return x < 0.0f ? -x : x;
}

struct knob_inertia_config {
	KNOB_PROFILE_CFG_ROM;
};

struct knob_inertia_data {
	float encoder_rpp;
	float last_angle;
	int32_t pulses;
	int32_t reported_pulses;

	float vel;   /* virtual flywheel velocity (rad/s) */
	float decay; /* per-tick velocity decay factor (0..1), web-tunable later */
};

static int knob_inertia_enable(const struct device *dev)
{
	const struct knob_inertia_config *cfg = dev->config;
	struct knob_inertia_data *data = dev->data;

	motor_set_torque_limit(cfg->motor, KNOB_PROFILE_TORQUE_LIMIT);

#if KNOB_PROFILE_HAS_VELOCITY_PID
	motor_set_velocity_pid(cfg->motor, KNOB_PROFILE_VELOCITY_PID);
#endif /* KNOB_PROFILE_HAS_VELOCITY_PID */

#if KNOB_PROFILE_HAS_ANGLE_PID
	motor_set_angle_pid(cfg->motor, KNOB_PROFILE_ANGLE_PID);
#endif /* KNOB_PROFILE_HAS_ANGLE_PID */

	data->last_angle = knob_get_position(cfg->knob);
	data->pulses = 0;
	data->reported_pulses = 0;

	data->vel = 0.0f;
	data->decay = KNOB_INERTIA_DEFAULT_DECAY;

	return 0;
}

static int knob_inertia_update_params(const struct device *dev, struct knob_params params)
{
	struct knob_inertia_data *data = dev->data;

	data->encoder_rpp = PI2 / (float)params.ppr;

	return 0;
}

static int knob_inertia_tick(const struct device *dev, struct motor_control *mc)
{
	const struct knob_inertia_config *cfg = dev->config;
	struct knob_inertia_data *data = dev->data;

	mc->mode = VELOCITY;

	float dp = knob_get_position(cfg->knob) - data->last_angle;
	float rpp = data->encoder_rpp;
	float rpp_2 = rpp / 2.0f;

	if (dp >= rpp_2) {
		data->last_angle += rpp;
		data->pulses++;
	} else if (dp <= -rpp_2) {
		data->last_angle -= rpp;
		data->pulses--;
	}

	/*
	 * Virtual flywheel. While the user spins it faster than the current coast,
	 * adopt their velocity — so the coast always starts at the speed you let go
	 * at, and a harder flick coasts faster and longer. Otherwise decay smoothly
	 * (exponential, viscous-friction-like) toward a stop; holding it still stops
	 * it. The motor is velocity-driven, so it physically spins down like a real
	 * weighted knob — continuous target, no per-detent notch.
	 */
	float v = knob_get_velocity(cfg->knob);
	if (absf(v) < KNOB_INERTIA_STOP) {
		data->vel = 0.0f;
	} else if (absf(v) > absf(data->vel) + KNOB_INERTIA_DEADBAND) {
		data->vel = v;
	} else {
		data->vel *= data->decay;
		if (absf(data->vel) < KNOB_INERTIA_STOP) {
			data->vel = 0.0f;
		}
	}

	mc->target = data->vel;

	return 0;
}

static int knob_inertia_report(const struct device *dev, int32_t *val)
{
	struct knob_inertia_data *data = dev->data;

	if (data->pulses == data->reported_pulses) {
		return -EAGAIN;
	}

	if (data->pulses > data->reported_pulses) {
		*val = 1;
	} else {
		*val = -1;
	}

	data->reported_pulses = data->pulses;

	return 0;
}

static int knob_inertia_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static const struct knob_profile_api knob_inertia_api = {
	.enable = knob_inertia_enable,
	.update_params = knob_inertia_update_params,
	.tick = knob_inertia_tick,
	.report = knob_inertia_report,
};

static struct knob_inertia_data knob_inertia_data;

static const struct knob_inertia_config knob_inertia_cfg = { KNOB_PROFILE_CFG_INIT };

DEVICE_DT_INST_DEFINE(0, knob_inertia_init, NULL, &knob_inertia_data, &knob_inertia_cfg,
		      POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &knob_inertia_api);
