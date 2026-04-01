/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TOUCHBAR_PREF_NAME_LEN 16

enum touchbar_mode {
	TOUCHBAR_MODE_DISABLE = 0,
	TOUCHBAR_MODE_PAN = 1,
	TOUCHBAR_MODE_APP_SWITCH = 2,
	TOUCHBAR_MODE_DESKTOP_SWITCH = 3,
};

struct touchbar_pref {
	bool active;
	char name[TOUCHBAR_PREF_NAME_LEN];
	enum touchbar_mode mode;
};

bool touchbar_app_get_enable(void);
void touchbar_app_set_enable(bool enable);

enum touchbar_mode touchbar_app_get_default_mode(void);
void touchbar_app_set_default_mode(enum touchbar_mode mode);

enum touchbar_mode touchbar_app_get_mode(void);

int touchbar_app_get_prefs(const struct touchbar_pref **prefs, const char ***names);
const struct touchbar_pref *touchbar_app_get_pref(uint8_t layer_id);
void touchbar_app_set_pref(uint8_t layer_id, const struct touchbar_pref *pref);
void touchbar_app_reset_pref(uint8_t layer_id);
