/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <app/diag_log.h>

#include "eink_app.h"
#include "eink_mode.h"
#include "eink_render.h"

/* NVS partition & key layout. */
#define EINK_FRAMES_PARTITION FIXED_PARTITION_ID(eink_frames_partition)
#define EINK_NVS_KEY_CONFIG  0x0001u
#define EINK_NVS_KEY_FRAME_BASE 0x1000u
#define EINK_NVS_FRAMES_PER_MODE 16u

/* Serialization header kept small & versioned for forward compatibility. */
#define EINK_CONFIG_MAGIC 0x454B4D44u /* 'EKMD' */
#define EINK_CONFIG_VERSION 1u

struct eink_config_blob {
	uint32_t magic;
	uint32_t version;
	uint8_t count;
	uint8_t active_index;
	uint8_t reserved[2];
	struct eink_mode_entry modes[EINK_MODE_CAPACITY];
} __packed;

/*
 * Partial-refresh policy.
 *
 * SSD16xx supports two refresh profiles:
 *   - "full":    black/white flash, re-initialises every pixel, no ghosting.
 *   - "partial": quiet single-pass blit, only pixels that actually changed
 *                are driven, but faint shadows ("ghosting") slowly build
 *                up across many successive partial updates.
 *
 * Clock/weather ticks at 60 s intervals want partial so the minute digits
 * can update without a disruptive flash; static frames / slideshow /
 * mode-switch want full because the new image bears little resemblance to
 * the previous one. After EINK_PARTIAL_MAX_STREAK consecutive partial
 * updates (≈ 30 min at the 1-min clock tick) we force one full refresh to
 * wipe accumulated ghosting.
 */
#define EINK_PARTIAL_MAX_STREAK 30U

/* Runtime state. */
static struct nvs_fs g_nvs;
static bool g_nvs_ready;
static struct eink_mode_view g_view;
static struct eink_render_clock g_clock;
static struct eink_render_weather g_weather;
static uint8_t g_slideshow_cursor;
static uint8_t g_frame_buf[EINK_FRAME_BYTES];
static struct k_mutex g_lock;
static struct k_work_delayable g_refresh_work;
static uint8_t g_partial_streak;
static bool g_force_full_next = true;

/*
 * Software clock: dynamic has no LSE + Vbat, so no real RTC. Every time the
 * helper pushes EINK_PUSH_CLOCK we remember {hour, minute, ...} together with
 * the k_uptime_get() value at that moment ("anchor"). A tick work then bumps
 * the visible time once per minute based on the elapsed uptime so the screen
 * keeps advancing even if the helper disconnects temporarily. The helper will
 * re-sync on its next push (every minute on the boundary) which corrects any
 * drift from the k_uptime oscillator.
 */
static int64_t g_clock_anchor_uptime_ms;
static struct k_work_delayable g_clock_tick_work;

enum render_reason {
	RENDER_REASON_IDLE = 0,
	RENDER_REASON_CONFIG = 1,
	RENDER_REASON_ACTIVE = 2,
	RENDER_REASON_FRAME_PUSH = 3,
	RENDER_REASON_CLOCK = 4,
	RENDER_REASON_WEATHER = 5,
	RENDER_REASON_SCHEDULED = 6,
};

static enum render_reason g_pending_reason;

static uint32_t frame_key(uint8_t mode_id, uint8_t frame_index)
{
	return EINK_NVS_KEY_FRAME_BASE + ((uint32_t)mode_id) * EINK_NVS_FRAMES_PER_MODE +
	       (uint32_t)frame_index;
}

static const struct eink_mode_entry *active_entry(void)
{
	if (g_view.count == 0 || g_view.active_index >= g_view.count) {
		return NULL;
	}
	return &g_view.modes[g_view.active_index];
}

static int eink_frames_nvs_init(void)
{
	const struct flash_area *fa;
	int ret = flash_area_open(EINK_FRAMES_PARTITION, &fa);
	if (ret != 0) {
		LOG_ERR("eink_frames open failed: %d", ret);
		return ret;
	}

	struct flash_sector sectors[4];
	uint32_t sector_cnt = ARRAY_SIZE(sectors);
	ret = flash_area_get_sectors(EINK_FRAMES_PARTITION, &sector_cnt, sectors);
	flash_area_close(fa);
	if (ret != 0) {
		LOG_ERR("eink_frames sectors failed: %d", ret);
		return ret;
	}

	if (sector_cnt < 2) {
		LOG_ERR("eink_frames partition needs at least 2 sectors (got %u)", sector_cnt);
		return -EINVAL;
	}

	g_nvs.offset = fa->fa_off;
	g_nvs.sector_size = sectors[0].fs_size;
	g_nvs.sector_count = sector_cnt;
	g_nvs.flash_device = fa->fa_dev;

	ret = nvs_mount(&g_nvs);
	if (ret != 0) {
		LOG_ERR("eink_frames nvs mount failed: %d", ret);
		return ret;
	}

	g_nvs_ready = true;
	LOG_INF("eink_frames NVS ready (%u sectors x %u bytes)", sector_cnt, sectors[0].fs_size);
	return 0;
}

static void apply_default_view(void)
{
	/*
	 * Preload three modes so "cycle forward / backward" via the keymap
	 * behaviour feels meaningful out of the box before the UI has ever
	 * saved a real config. Users can override all of this via EINK_SET_CONFIG.
	 */
	memset(&g_view, 0, sizeof(g_view));
	g_view.count = 3;
	g_view.active_index = 0;

	g_view.modes[0].id = 1;
	g_view.modes[0].type = EINK_MODE_TYPE_CLOCK_WEATHER;
	g_view.modes[0].refresh_interval_s = 60;
	g_view.modes[0].frame_count = 0;
	strncpy(g_view.modes[0].label, "Clock", EINK_MODE_LABEL_LEN - 1);

	g_view.modes[1].id = 2;
	g_view.modes[1].type = EINK_MODE_TYPE_STATIC;
	g_view.modes[1].refresh_interval_s = 0;
	g_view.modes[1].frame_count = 1;
	strncpy(g_view.modes[1].label, "Image", EINK_MODE_LABEL_LEN - 1);

	g_view.modes[2].id = 3;
	g_view.modes[2].type = EINK_MODE_TYPE_OFF;
	g_view.modes[2].refresh_interval_s = 0;
	g_view.modes[2].frame_count = 0;
	strncpy(g_view.modes[2].label, "Off", EINK_MODE_LABEL_LEN - 1);
}

static int load_config_from_nvs(void)
{
	if (!g_nvs_ready) {
		return -ENODEV;
	}

	struct eink_config_blob blob;
	ssize_t got = nvs_read(&g_nvs, EINK_NVS_KEY_CONFIG, &blob, sizeof(blob));
	if (got != sizeof(blob)) {
		return (int)got;
	}
	if (blob.magic != EINK_CONFIG_MAGIC || blob.version != EINK_CONFIG_VERSION) {
		LOG_WRN("eink config magic/version mismatch (%x/%u)", blob.magic, blob.version);
		return -EINVAL;
	}

	g_view.count = MIN(blob.count, (uint8_t)EINK_MODE_CAPACITY);
	g_view.active_index = (blob.active_index < g_view.count) ? blob.active_index : 0;
	for (uint8_t i = 0; i < g_view.count; i++) {
		g_view.modes[i] = blob.modes[i];
		g_view.modes[i].label[EINK_MODE_LABEL_LEN - 1] = '\0';
	}
	return 0;
}

static int save_config_to_nvs(void)
{
	if (!g_nvs_ready) {
		return -ENODEV;
	}

	struct eink_config_blob blob = {
		.magic = EINK_CONFIG_MAGIC,
		.version = EINK_CONFIG_VERSION,
		.count = g_view.count,
		.active_index = g_view.active_index,
	};
	for (uint8_t i = 0; i < g_view.count; i++) {
		blob.modes[i] = g_view.modes[i];
	}

	ssize_t ret = nvs_write(&g_nvs, EINK_NVS_KEY_CONFIG, &blob, sizeof(blob));
	return ret >= 0 ? 0 : (int)ret;
}

static int load_frame_from_nvs(uint8_t mode_id, uint8_t frame_index, uint8_t *out,
			       size_t out_size)
{
	if (!g_nvs_ready) {
		return -ENODEV;
	}
	ssize_t got = nvs_read(&g_nvs, frame_key(mode_id, frame_index), out, out_size);
	if (got < 0) {
		return (int)got;
	}
	return got == (ssize_t)out_size ? 0 : -ENODATA;
}

static void trigger_refresh(enum render_reason reason, k_timeout_t delay)
{
	g_pending_reason = reason;
	k_work_reschedule(&g_refresh_work, delay);
}

static void log_render(uint8_t active_index, uint8_t type, uint8_t cursor, uint8_t reason)
{
	uint32_t data0 = hw75_diag_pack_u8x4(active_index, type, cursor, reason);
	hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_EINK,
			    hw75_diag_next_trace_id(), HW75_DIAG_EVENT_EINK_RENDER, data0, false,
			    0U);
}

static int push_frame_buf_to_eink(bool prefer_partial)
{
	bool use_partial = prefer_partial && !g_force_full_next &&
			   g_partial_streak < EINK_PARTIAL_MAX_STREAK;
	int ret = eink_update(g_frame_buf, EINK_FRAME_BYTES, use_partial);
	if (ret != 0) {
		return ret;
	}
	if (use_partial) {
		g_partial_streak++;
	} else {
		g_partial_streak = 0;
		g_force_full_next = false;
	}
	return 0;
}

static int render_static(const struct eink_mode_entry *mode)
{
	int ret = load_frame_from_nvs(mode->id, 0, g_frame_buf, EINK_FRAME_BYTES);
	if (ret != 0) {
		/* No frame uploaded yet -> keep whatever the panel was showing. */
		LOG_WRN("Static mode %u has no frame yet (%d)", mode->id, ret);
		return 0;
	}
	return push_frame_buf_to_eink(false);
}

static int render_slideshow(const struct eink_mode_entry *mode)
{
	uint8_t count = MIN(mode->frame_count, (uint8_t)EINK_FRAME_CAPACITY);
	if (count == 0) {
		return 0;
	}

	uint8_t idx = g_slideshow_cursor % count;
	int ret = load_frame_from_nvs(mode->id, idx, g_frame_buf, EINK_FRAME_BYTES);
	g_slideshow_cursor = (idx + 1u) % count;
	if (ret != 0) {
		LOG_WRN("Slideshow frame %u/%u missing (%d)", mode->id, idx, ret);
		return 0;
	}
	return push_frame_buf_to_eink(false);
}

static int render_clock_weather(void)
{
	/*
	 * Don't overwrite the E-Ink panel with a "00:00 -- degC" placeholder
	 * before the helper has pushed any real data yet. Leaving the panel
	 * untouched preserves whatever it was showing on the previous power-on
	 * (the hardware retains the image), which is much nicer when the
	 * dynamic is temporarily unplugged or the host hasn't started
	 * helper-core yet.
	 */
	if (!g_clock.valid && !g_weather.valid) {
		return 0;
	}
	eink_render_clock_weather(g_frame_buf, &g_clock, &g_weather);
	return push_frame_buf_to_eink(true);
}

static int render_off(void)
{
	eink_render_clear(g_frame_buf);
	return push_frame_buf_to_eink(false);
}

static void schedule_next_tick(const struct eink_mode_entry *mode)
{
	if (!mode || mode->refresh_interval_s == 0) {
		return;
	}
	k_work_reschedule(&g_refresh_work, K_SECONDS(mode->refresh_interval_s));
}

static void refresh_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&g_lock, K_FOREVER);
	const struct eink_mode_entry *mode = active_entry();
	enum render_reason reason = g_pending_reason;
	g_pending_reason = RENDER_REASON_IDLE;

	if (!mode) {
		log_render(0xFFu, 0xFFu, 0, (uint8_t)reason);
		k_mutex_unlock(&g_lock);
		return;
	}

	log_render(g_view.active_index, (uint8_t)mode->type, g_slideshow_cursor,
		   (uint8_t)reason);

	switch (mode->type) {
	case EINK_MODE_TYPE_OFF:
		render_off();
		break;
	case EINK_MODE_TYPE_STATIC:
		render_static(mode);
		break;
	case EINK_MODE_TYPE_SLIDESHOW:
		render_slideshow(mode);
		schedule_next_tick(mode);
		break;
	case EINK_MODE_TYPE_CLOCK_WEATHER:
		render_clock_weather();
		schedule_next_tick(mode);
		break;
	}

	k_mutex_unlock(&g_lock);
}

void eink_mode_get_view(struct eink_mode_view *view)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	memcpy(view, &g_view, sizeof(*view));
	k_mutex_unlock(&g_lock);
}

int eink_mode_set_config(const struct eink_mode_entry *modes, uint8_t count, uint8_t active_index)
{
	if (count > EINK_MODE_CAPACITY) {
		return -EINVAL;
	}

	k_mutex_lock(&g_lock, K_FOREVER);

	uint8_t prev_id = 0xFFu;
	uint8_t prev_type = 0xFFu;
	if (g_view.count > 0 && g_view.active_index < g_view.count) {
		prev_id = g_view.modes[g_view.active_index].id;
		prev_type = (uint8_t)g_view.modes[g_view.active_index].type;
	}

	g_view.count = count;
	g_view.active_index = (count == 0) ? 0 : (active_index < count ? active_index : 0);
	/* Config rewrite changes the landscape wholesale; next render must
	 * be a full refresh to avoid partial-blending stale panel pixels. */
	g_force_full_next = true;
	for (uint8_t i = 0; i < count; i++) {
		g_view.modes[i] = modes[i];
		g_view.modes[i].label[EINK_MODE_LABEL_LEN - 1] = '\0';
	}
	g_slideshow_cursor = 0;

	int ret = save_config_to_nvs();
	if (ret != 0) {
		LOG_ERR("eink config save failed: %d", ret);
	}

	uint8_t new_id = 0xFFu;
	uint8_t new_type = 0xFFu;
	if (count > 0) {
		new_id = g_view.modes[g_view.active_index].id;
		new_type = (uint8_t)g_view.modes[g_view.active_index].type;
	}

	hw75_diag_log_event(
		HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_EINK, hw75_diag_next_trace_id(),
		HW75_DIAG_EVENT_EINK_MODE_CHANGED,
		hw75_diag_pack_u8x4(prev_id, new_id, prev_type, new_type), false, 0U);

	trigger_refresh(RENDER_REASON_CONFIG, K_NO_WAIT);
	k_mutex_unlock(&g_lock);
	return 0;
}

int eink_mode_set_active(uint8_t active_index)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	if (g_view.count == 0) {
		k_mutex_unlock(&g_lock);
		return -ENODATA;
	}
	if (active_index >= g_view.count) {
		k_mutex_unlock(&g_lock);
		return -EINVAL;
	}

	uint8_t prev_index = g_view.active_index;
	if (prev_index != active_index) {
		uint8_t prev_id = g_view.modes[prev_index].id;
		uint8_t new_id = g_view.modes[active_index].id;
		g_view.active_index = active_index;
		g_slideshow_cursor = 0;
		/* New content has no relation to the old panel image; force a
		 * full refresh so the next render starts from a clean slate. */
		g_force_full_next = true;
		save_config_to_nvs();
		hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_EINK,
				    hw75_diag_next_trace_id(),
				    HW75_DIAG_EVENT_EINK_MODE_CHANGED,
				    hw75_diag_pack_u8x4(prev_id, new_id,
							(uint8_t)g_view.modes[prev_index].type,
							(uint8_t)g_view.modes[active_index].type),
				    false, 0U);
	}

	trigger_refresh(RENDER_REASON_ACTIVE, K_NO_WAIT);
	k_mutex_unlock(&g_lock);
	return 0;
}

int eink_mode_cycle(int delta)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	if (g_view.count == 0) {
		k_mutex_unlock(&g_lock);
		return -ENODATA;
	}
	int count = (int)g_view.count;
	int next = ((int)g_view.active_index + delta) % count;
	if (next < 0) {
		next += count;
	}
	k_mutex_unlock(&g_lock);
	return eink_mode_set_active((uint8_t)next);
}

int eink_mode_push_frame(uint8_t mode_id, uint8_t frame_index, const uint8_t *bits,
			 uint32_t bits_len)
{
	if (!bits || bits_len != EINK_FRAME_BYTES) {
		return -EINVAL;
	}
	if (frame_index >= EINK_NVS_FRAMES_PER_MODE) {
		return -EINVAL;
	}
	if (!g_nvs_ready) {
		return -ENODEV;
	}

	ssize_t ret = nvs_write(&g_nvs, frame_key(mode_id, frame_index), bits, bits_len);
	if (ret < 0) {
		LOG_ERR("eink frame write failed: %d", (int)ret);
		return (int)ret;
	}

	hw75_diag_log_event(HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_EINK,
			    hw75_diag_next_trace_id(), HW75_DIAG_EVENT_EINK_FRAME_RECEIVED,
			    hw75_diag_pack_u16x2(mode_id, frame_index), true, bits_len);

	k_mutex_lock(&g_lock, K_FOREVER);
	const struct eink_mode_entry *mode = active_entry();
	bool should_refresh = mode && mode->id == mode_id &&
			     (mode->type == EINK_MODE_TYPE_STATIC ||
			      mode->type == EINK_MODE_TYPE_SLIDESHOW);
	if (should_refresh) {
		trigger_refresh(RENDER_REASON_FRAME_PUSH, K_NO_WAIT);
	}
	k_mutex_unlock(&g_lock);
	return 0;
}

static void reschedule_clock_tick(void)
{
	/* Wake up on the next minute boundary, relative to the anchor uptime. */
	int64_t since_anchor = k_uptime_get() - g_clock_anchor_uptime_ms;
	int64_t ms_into_minute = since_anchor % 60000;
	int64_t delay = 60000 - ms_into_minute;
	if (delay < 500) {
		delay += 60000;
	}
	k_work_reschedule(&g_clock_tick_work, K_MSEC(delay));
}

static void advance_clock_once(void)
{
	/* Called under g_lock. Push forward by one minute and re-render. */
	if (!g_clock.valid) {
		return;
	}
	g_clock.minute++;
	if (g_clock.minute >= 60) {
		g_clock.minute = 0;
		g_clock.hour++;
		if (g_clock.hour >= 24) {
			g_clock.hour = 0;
			/* Day / weekday rollover is too involved for a software
			 * clock without a real date library; the helper resync
			 * will correct day/month/weekday within a minute.
			 */
			if (g_clock.weekday < 6) {
				g_clock.weekday++;
			} else {
				g_clock.weekday = 0;
			}
		}
	}
}

static void clock_tick_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&g_lock, K_FOREVER);

	advance_clock_once();

	const struct eink_mode_entry *mode = active_entry();
	bool is_clock = mode && mode->type == EINK_MODE_TYPE_CLOCK_WEATHER;
	if (is_clock) {
		trigger_refresh(RENDER_REASON_CLOCK, K_NO_WAIT);
	}

	reschedule_clock_tick();
	k_mutex_unlock(&g_lock);
}

int eink_mode_push_clock(uint8_t hour, uint8_t minute, uint8_t day, uint8_t month, uint8_t weekday,
			 uint16_t year)
{
	k_mutex_lock(&g_lock, K_FOREVER);
	g_clock.hour = hour;
	g_clock.minute = minute;
	g_clock.day = day;
	g_clock.month = month;
	g_clock.weekday = weekday;
	g_clock.year = year;
	g_clock.has_date = (day != 0 && month != 0);
	g_clock.valid = true;
	g_clock_anchor_uptime_ms = k_uptime_get();

	const struct eink_mode_entry *mode = active_entry();
	bool is_clock = mode && mode->type == EINK_MODE_TYPE_CLOCK_WEATHER;
	if (is_clock) {
		trigger_refresh(RENDER_REASON_CLOCK, K_NO_WAIT);
	}

	reschedule_clock_tick();
	k_mutex_unlock(&g_lock);

	hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_HELPER_CORE,
			    hw75_diag_next_trace_id(), HW75_DIAG_EVENT_HELPER_CLOCK_SYNC,
			    hw75_diag_pack_u8x4(hour, minute, day, month),
			    true, hw75_diag_pack_u8x4(weekday, 0, 0, 0));
	return 0;
}

int eink_mode_push_weather(int16_t temp_deci_c, uint8_t icon_id, const char *city)
{
	if (icon_id >= EINK_WEATHER_ICON_COUNT) {
		icon_id = EINK_WEATHER_UNKNOWN;
	}

	k_mutex_lock(&g_lock, K_FOREVER);
	g_weather.temp_deci_c = temp_deci_c;
	g_weather.icon = icon_id;
	if (city) {
		strncpy(g_weather.city, city, sizeof(g_weather.city) - 1);
		g_weather.city[sizeof(g_weather.city) - 1] = '\0';
	}
	g_weather.valid = true;

	const struct eink_mode_entry *mode = active_entry();
	bool is_clock = mode && mode->type == EINK_MODE_TYPE_CLOCK_WEATHER;
	if (is_clock) {
		trigger_refresh(RENDER_REASON_WEATHER, K_NO_WAIT);
	}
	k_mutex_unlock(&g_lock);

	hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_HELPER_CORE,
			    hw75_diag_next_trace_id(), HW75_DIAG_EVENT_HELPER_WEATHER_SYNC,
			    hw75_diag_pack_s16x2(temp_deci_c, (int16_t)icon_id), false, 0U);
	return 0;
}

uint8_t eink_mode_get_active_index(void)
{
	return g_view.active_index;
}

uint8_t eink_mode_get_active_id(void)
{
	const struct eink_mode_entry *mode = active_entry();
	return mode ? mode->id : 0xFFu;
}

enum eink_mode_type eink_mode_get_active_type(void)
{
	const struct eink_mode_entry *mode = active_entry();
	return mode ? mode->type : EINK_MODE_TYPE_OFF;
}

static int eink_mode_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	k_mutex_init(&g_lock);
	k_work_init_delayable(&g_refresh_work, refresh_work_handler);
	k_work_init_delayable(&g_clock_tick_work, clock_tick_handler);

	int ret = eink_frames_nvs_init();
	if (ret != 0) {
		LOG_ERR("eink mode NVS init failed, falling back to defaults");
	}

	if (load_config_from_nvs() != 0) {
		apply_default_view();
	}

	/* Defer first render a couple seconds so the display driver is ready. */
	trigger_refresh(RENDER_REASON_ACTIVE, K_SECONDS(2));
	return 0;
}

SYS_INIT(eink_mode_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
