/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#ifndef KNOB_INCLUDE_DRIVERS_KNOB_H_
#define KNOB_INCLUDE_DRIVERS_KNOB_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

/**
 * @file
 * @brief Extended public API for knob
 */

#ifdef __cplusplus
extern "C" {
#endif

enum knob_mode {
	KNOB_DISABLE = 0,
	KNOB_INERTIA,
	KNOB_ENCODER,
	KNOB_SPRING,
	KNOB_DAMPED,
	KNOB_SPIN,
	KNOB_RATCHET,
	KNOB_SWITCH,
};

struct knob_params {
	int ppr;
};

void knob_set_mode(const struct device *dev, enum knob_mode mode);

enum knob_mode knob_get_mode(const struct device *dev);

void knob_set_enable(const struct device *dev, bool enable);

void knob_set_encoder_report(const struct device *dev, bool report);

bool knob_get_encoder_report(const struct device *dev);

void knob_set_encoder_ppr(const struct device *dev, int ppr);

int knob_get_encoder_ppr(const struct device *dev);

void knob_set_position_limit(const struct device *dev, float min, float max);

void knob_get_position_limit(const struct device *dev, float *min, float *max);

float knob_get_position(const struct device *dev);

float knob_get_velocity(const struct device *dev);

/**
 * @brief Set the logical zero for knob position (radians).
 *
 * This is where profiles with a natural "centre" (e.g. spring) will rest the
 * motor. It only shifts profile behaviour; motor FOC calibration is untouched.
 */
void knob_set_position_offset(const struct device *dev, float offset);

float knob_get_position_offset(const struct device *dev);

/**
 * @brief Fire a one-shot haptic pulse: a brief transient ANGLE out-and-back
 * "bump" felt in the fingertips, overriding the active profile for a few ticks
 * and then resuming it. Net displacement is ~zero, so no spurious encoder
 * report is generated. No-op while the knob is disabled (motor off).
 *
 * @param strength 0..100 nudge firmness (clamped).
 * @param count    number of bumps (>=1).
 */
void knob_pulse(const struct device *dev, uint8_t strength, uint8_t count);

/**
 * @brief Reshape the dial into a host-defined detent map: @p count notches via
 * the encoder profile, optionally walled by hard end-stops at both ends
 * (Surface-Dial / list-picker feel).
 *
 * @param count    number of detents (>=1).
 * @param strength 0..100 detent firmness (motor torque).
 * @param endstops true = hard walls at the first/last detent.
 */
void knob_set_detents(const struct device *dev, uint8_t count, uint8_t strength, bool endstops);

#ifdef __cplusplus
}
#endif

#endif /* KNOB_INCLUDE_DRIVERS_KNOB_H_ */
