/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/debounce.h>
#include <zmk/matrix.h>

#include <dt-bindings/zmk/matrix_transform.h>

#include <app/kscan_74hc165.h>
#include <app/diag_log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_kscan_gpio_74hc165

#if CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS >= 0
#define INST_DEBOUNCE_PRESS_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS
#else
#define INST_DEBOUNCE_PRESS_MS(n)                                                                  \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_press_ms))
#endif

#if CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS >= 0
#define INST_DEBOUNCE_RELEASE_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS
#else
#define INST_DEBOUNCE_RELEASE_MS(n)                                                                \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_release_ms))
#endif

#define NGPIOS 8

struct kscan_74hc165_data {
    const struct device *dev;
    kscan_callback_t callback;
    struct k_work_delayable work;
    /** Timestamp of the current or scheduled scan. */
    int64_t scan_time;
    /** Current state of the inputs as an array of length config->inputs.len */
    struct zmk_debounce_state *pin_state;
    uint8_t *read_buf;
    uint8_t touchbar_state;
    uint8_t raw_touchbar_state;
    uint8_t debounced_touchbar_state;
    int64_t raw_state_timestamp;
    struct k_spinlock lock;
};

struct kscan_74hc165_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec load_gpio;
    struct zmk_debounce_config debounce_config;
    uint16_t chain_length;
    const uint8_t *scan_masks;
    int32_t debounce_scan_period_ms;
    int32_t poll_period_ms;
};

#if defined(CONFIG_BOARD_HW75_KEYBOARD)

static const uint8_t hw75_touchbar_rows[HW75_TOUCHBAR_CHANNEL_COUNT] = {
    10, 10, 10, 10, 10, 10,
};

static const uint8_t hw75_touchbar_cols[HW75_TOUCHBAR_CHANNEL_COUNT] = {
    /*
     * Verified on hardware with a temporary key-backlight overlay that mapped
     * the six logical TouchBar channels onto the number-row LEDs 1..6.
     * Physical touch points from left to right map to row 10 columns in this
     * order.
     */
    4, 1, 3, 5, 0, 2,
};

static bool kscan_74hc165_touchbar_raw_pressed(const struct kscan_74hc165_data *data,
                                               const struct kscan_74hc165_config *config,
                                               uint8_t row, uint8_t col) {
    uint8_t bits = data->read_buf[row] | ~config->scan_masks[row];
    return (bits & BIT(col)) == 0U;
}

static uint8_t kscan_74hc165_decode_touchbar_state(const struct kscan_74hc165_data *data,
                                                   const struct kscan_74hc165_config *config,
                                                   bool use_debounced) {
    uint8_t logical_touch_state = 0U;

    for (int logical_pos = 0; logical_pos < HW75_TOUCHBAR_CHANNEL_COUNT; logical_pos++) {
        uint8_t row = hw75_touchbar_rows[logical_pos];
        uint8_t col = hw75_touchbar_cols[logical_pos];

        if (row >= config->chain_length || col >= NGPIOS) {
            continue;
        }

        bool pressed = use_debounced
                               ? zmk_debounce_is_pressed(&data->pin_state[row * NGPIOS + col])
                               : kscan_74hc165_touchbar_raw_pressed(data, config, row, col);

        if (pressed) {
            logical_touch_state |= BIT(HW75_TOUCHBAR_CHANNEL_COUNT - 1U - logical_pos);
        }
    }

    return logical_touch_state;
}

void hw75_kscan_74hc165_get_touchbar_logical_map(uint8_t rows[HW75_TOUCHBAR_CHANNEL_COUNT],
                                                 uint8_t cols[HW75_TOUCHBAR_CHANNEL_COUNT]) {
    if (rows != NULL) {
        memcpy(rows, hw75_touchbar_rows, sizeof(hw75_touchbar_rows));
    }
    if (cols != NULL) {
        memcpy(cols, hw75_touchbar_cols, sizeof(hw75_touchbar_cols));
    }
}

#else

static uint8_t kscan_74hc165_decode_touchbar_state(const struct kscan_74hc165_data *data,
                                                   const struct kscan_74hc165_config *config,
                                                   bool use_debounced) {
    ARG_UNUSED(data);
    ARG_UNUSED(config);
    ARG_UNUSED(use_debounced);
    return 0U;
}

void hw75_kscan_74hc165_get_touchbar_logical_map(uint8_t rows[HW75_TOUCHBAR_CHANNEL_COUNT],
                                                 uint8_t cols[HW75_TOUCHBAR_CHANNEL_COUNT]) {
    if (rows != NULL) {
        memset(rows, 0, HW75_TOUCHBAR_CHANNEL_COUNT);
    }
    if (cols != NULL) {
        memset(cols, 0, HW75_TOUCHBAR_CHANNEL_COUNT);
    }
}

#endif

static void kscan_74hc165_read_continue(const struct device *dev) {
    const struct kscan_74hc165_config *config = dev->config;
    struct kscan_74hc165_data *data = dev->data;

    data->scan_time += config->debounce_scan_period_ms;

    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static void kscan_74hc165_read_end(const struct device *dev) {
    struct kscan_74hc165_data *data = dev->data;
    const struct kscan_74hc165_config *config = dev->config;

    data->scan_time += config->poll_period_ms;

    // Return to polling slowly.
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static int kscan_74hc165_read(const struct device *dev) {
    struct kscan_74hc165_data *data = dev->data;
    const struct kscan_74hc165_config *config = dev->config;

    const struct spi_buf rx_buf = {
        .buf = data->read_buf,
        .len = config->chain_length,
    };
    const struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1U,
    };

    gpio_pin_set_dt(&config->load_gpio, true);
    gpio_pin_set_dt(&config->load_gpio, false);
    spi_read_dt(&config->bus, &rx_bufs);

    uint8_t bits;
    for (int i = 0; i < config->chain_length; i++) {
        bits = data->read_buf[i] | ~config->scan_masks[i];
        for (int j = 0; j < NGPIOS; j++) {
            zmk_debounce_update(&data->pin_state[i * NGPIOS + j], (bits & BIT(j)) == 0,
                                config->debounce_scan_period_ms, &config->debounce_config);
        }
    }

    // Process the new state.
    bool continue_scan = false;

    for (int i = 0; i < config->chain_length; i++) {
        for (int j = 0; j < NGPIOS; j++) {
            struct zmk_debounce_state *state = &data->pin_state[i * NGPIOS + j];

            if (zmk_debounce_get_changed(state)) {
                const bool pressed = zmk_debounce_is_pressed(state);
                LOG_DBG("Sending event at %i,%i state %s", i, j, pressed ? "on" : "off");
                data->callback(dev, i, j, pressed);
            }

            continue_scan = continue_scan || zmk_debounce_is_active(state);
        }
    }

    uint8_t raw_touchbar_state = kscan_74hc165_decode_touchbar_state(data, config, false);
    uint8_t debounced_touchbar_state = kscan_74hc165_decode_touchbar_state(data, config, true);
    uint8_t previous_raw_touchbar_state = data->raw_touchbar_state;
    uint8_t previous_debounced_touchbar_state = data->debounced_touchbar_state;
    uint8_t previous_touchbar_state = data->touchbar_state;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->raw_touchbar_state = raw_touchbar_state;
    data->debounced_touchbar_state = debounced_touchbar_state;
    data->touchbar_state = debounced_touchbar_state;
    data->raw_state_timestamp = k_uptime_get();
    k_spin_unlock(&data->lock, key);

    if (previous_raw_touchbar_state != raw_touchbar_state ||
        previous_debounced_touchbar_state != debounced_touchbar_state ||
        previous_touchbar_state != debounced_touchbar_state) {
        hw75_diag_log_event(HW75_DIAG_LEVEL_DEBUG, HW75_DIAG_MODULE_KSCAN, 0U,
                            HW75_DIAG_EVENT_KSCAN_TOUCH_STATE,
                            hw75_diag_pack_u8x4(raw_touchbar_state, debounced_touchbar_state,
                                                debounced_touchbar_state, 0U),
                            false, 0U);
        hw75_diag_update_snapshot(HW75_DIAG_MODULE_KSCAN,
                                  hw75_diag_pack_u8x4(raw_touchbar_state,
                                                      debounced_touchbar_state,
                                                      debounced_touchbar_state, 0U),
                                  0U, 0U);
    }

    if (continue_scan) {
        // At least one key is pressed or the debouncer has not yet decided if
        // it is pressed. Poll quickly until everything is released.
        kscan_74hc165_read_continue(dev);
    } else {
        // All keys are released. Return to normal.
        kscan_74hc165_read_end(dev);
    }

    return 0;
}

static void kscan_74hc165_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct kscan_74hc165_data *data = CONTAINER_OF(dwork, struct kscan_74hc165_data, work);
    kscan_74hc165_read(data->dev);
}

static int kscan_74hc165_configure(const struct device *dev, kscan_callback_t callback) {
    struct kscan_74hc165_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

static int kscan_74hc165_enable(const struct device *dev) {
    struct kscan_74hc165_data *data = dev->data;
    data->scan_time = k_uptime_get();
    return kscan_74hc165_read(dev);
}

static int kscan_74hc165_disable(const struct device *dev) {
    struct kscan_74hc165_data *data = dev->data;
    k_work_cancel_delayable(&data->work);
    return 0;
}

static int kscan_74hc165_init(const struct device *dev) {
    struct kscan_74hc165_data *data = dev->data;
    const struct kscan_74hc165_config *config = dev->config;

    data->dev = dev;

    if (!device_is_ready(config->load_gpio.port)) {
        LOG_ERR("GPIO is not ready: %s", config->load_gpio.port->name);
        return -ENODEV;
    }
    if (!device_is_ready(config->bus.bus)) {
        LOG_ERR("SPI bus is not ready: %s", config->bus.bus->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&config->load_gpio, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for input", config->load_gpio.pin,
                config->load_gpio.port->name);
        return err;
    }

    k_work_init_delayable(&data->work, kscan_74hc165_work_handler);

#if defined(CONFIG_BOARD_HW75_KEYBOARD)
    hw75_diag_log_event(
        HW75_DIAG_LEVEL_INFO, HW75_DIAG_MODULE_KSCAN, 0U, HW75_DIAG_EVENT_KSCAN_TOUCH_MAP,
        ((uint32_t)hw75_touchbar_rows[0] & 0xFU) | (((uint32_t)hw75_touchbar_cols[0] & 0xFU) << 4) |
            (((uint32_t)hw75_touchbar_rows[1] & 0xFU) << 8) |
            (((uint32_t)hw75_touchbar_cols[1] & 0xFU) << 12) |
            (((uint32_t)hw75_touchbar_rows[2] & 0xFU) << 16) |
            (((uint32_t)hw75_touchbar_cols[2] & 0xFU) << 20) |
            (((uint32_t)hw75_touchbar_rows[3] & 0xFU) << 24) |
            (((uint32_t)hw75_touchbar_cols[3] & 0xFU) << 28),
        true,
        ((uint32_t)hw75_touchbar_rows[4] & 0xFU) | (((uint32_t)hw75_touchbar_cols[4] & 0xFU) << 4) |
            (((uint32_t)hw75_touchbar_rows[5] & 0xFU) << 8) |
            (((uint32_t)hw75_touchbar_cols[5] & 0xFU) << 12));
#endif

    return 0;
}

static const struct kscan_driver_api kscan_74hc165_api = {
    .config = kscan_74hc165_configure,
    .enable_callback = kscan_74hc165_enable,
    .disable_callback = kscan_74hc165_disable,
};

#define KSCAN_74HC165_INIT(n)                                                                      \
    BUILD_ASSERT(INST_DEBOUNCE_PRESS_MS(n) <= DEBOUNCE_COUNTER_MAX,                                \
                 "ZMK_KSCAN_DEBOUNCE_PRESS_MS or debounce-press-ms is too large");                 \
    BUILD_ASSERT(INST_DEBOUNCE_RELEASE_MS(n) <= DEBOUNCE_COUNTER_MAX,                              \
                 "ZMK_KSCAN_DEBOUNCE_RELEASE_MS or debounce-release-ms is too large");             \
                                                                                                   \
    static struct zmk_debounce_state kscan_74hc165_state_##n[DT_INST_PROP(n, chain_length) * 8];   \
                                                                                                   \
    static uint8_t kscan_74hc165_read_buf_##n[DT_INST_PROP(n, chain_length)];                      \
                                                                                                   \
    static struct kscan_74hc165_data kscan_74hc165_data_##n = {                                    \
        .pin_state = kscan_74hc165_state_##n,                                                      \
        .read_buf = kscan_74hc165_read_buf_##n,                                                    \
    };                                                                                             \
                                                                                                   \
    static uint8_t kscan_74hc165_scan_masks_##n[DT_INST_PROP(n, chain_length)] =                   \
        DT_INST_PROP(n, scan_masks);                                                               \
                                                                                                   \
    static struct kscan_74hc165_config kscan_74hc165_config_##n = {                                \
        .bus =                                                                                     \
            SPI_DT_SPEC_INST_GET(n, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),   \
        .load_gpio = GPIO_DT_SPEC_INST_GET(n, parallel_load_gpios),                                \
        .debounce_config =                                                                         \
            {                                                                                      \
                .debounce_press_ms = INST_DEBOUNCE_PRESS_MS(n),                                    \
                .debounce_release_ms = INST_DEBOUNCE_RELEASE_MS(n),                                \
            },                                                                                     \
        .chain_length = DT_INST_PROP(n, chain_length),                                             \
        .scan_masks = kscan_74hc165_scan_masks_##n,                                                \
        .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),                       \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                         \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, &kscan_74hc165_init, NULL, &kscan_74hc165_data_##n,                   \
                          &kscan_74hc165_config_##n, APPLICATION,                                  \
                          CONFIG_APPLICATION_INIT_PRIORITY, &kscan_74hc165_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_74HC165_INIT);

int hw75_kscan_74hc165_get_touchbar_state(const struct device *dev, uint8_t *state,
                                          int64_t *timestamp) {
    if (dev == NULL || state == NULL) {
        return -EINVAL;
    }

    struct kscan_74hc165_data *data = dev->data;
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    *state = data->touchbar_state;
    if (timestamp != NULL) {
        *timestamp = data->raw_state_timestamp;
    }
    k_spin_unlock(&data->lock, key);

    return 0;
}
