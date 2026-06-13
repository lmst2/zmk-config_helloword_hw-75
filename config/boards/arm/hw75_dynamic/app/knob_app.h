/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#define KNOB_PREF_NAME_LEN 16

struct knob_pref {
	bool active;
	char name[KNOB_PREF_NAME_LEN];
	enum knob_mode mode;
	int ppr;
	float torque_limit;
	float inertia_damping; /* 0..100 INERTIA flywheel damping; low = long coast */
};

bool knob_app_get_demo(void);
void knob_app_set_demo(bool demo);

int knob_app_get_prefs(const struct knob_pref **prefs, const char ***names);
const struct knob_pref *knob_app_get_pref(uint8_t layer_id);
void knob_app_set_pref(uint8_t layer_id, struct knob_pref *pref);
void knob_app_reset_pref(uint8_t layer_id);

int knob_app_set_calibration(float zero_offset, int direction);
int knob_app_recalibrate_auto(void);

/* Keep the knob enabled while an external source (the keyboard, over UART)
 * reports activity, even if the dynamic module's own input has gone idle. */
void knob_app_set_external_active(bool active);

/* Brief haptic bump to confirm an on-device action (e.g. TouchBar mode change). */
void knob_app_pulse(void);
