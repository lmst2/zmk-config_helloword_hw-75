/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_uart_slip

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_slip, CONFIG_UART_SLIP_LOG_LEVEL);

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/console/uart_slip.h>

#include <zephyr/sys/ring_buffer.h>

#define SLIP_END 0300
#define SLIP_ESC 0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335

enum uart_slip_state {
	STATE_SKIP,
	STATE_BYTE,
	STATE_ESC,
};

struct uart_slip_data {
	uint8_t rx_buf[CONFIG_UART_SLIP_RX_RING_BUFFER_SIZE];
	struct ring_buf rx_rb;

	enum uart_slip_state state;
};

struct uart_slip_config {
	const struct device *uart;
};

/*
 * Total wall-clock budget for sending one SLIP frame. At 115200 bps each byte
 * takes ~87us; a worst-case 64-byte frame with full escaping would take
 * ~11ms to send normally, so 20ms leaves a generous margin while still
 * failing fast when the peer has pinned the TX line (e.g. dynamic sitting in
 * its UF2 bootloader). Without this we would busy-loop inside
 * uart_poll_out()'s "while (!TXE) {}" forever and deadlock whatever work /
 * thread called us.
 */
#define UART_SLIP_SEND_BUDGET_MS 20

static int send_byte(const struct device *uart, uint8_t b, int64_t deadline_ms)
{
	while (true) {
		/*
		 * uart_fifo_fill() is non-blocking: it returns 0 when TX is
		 * not ready (TXE=0 on STM32 without FIFO) and 1 once it has
		 * written the byte, all without busy-waiting inside the
		 * driver. We do the waiting ourselves with a deadline so a
		 * stuck line can never hold the caller hostage.
		 */
		int sent = uart_fifo_fill(uart, &b, 1);
		if (sent == 1) {
			return 0;
		}

		if (k_uptime_get() >= deadline_ms) {
			return -ETIMEDOUT;
		}

		k_yield();
	}
}

int uart_slip_send(const struct device *dev, const uint8_t *buf, uint32_t len)
{
	const struct uart_slip_config *config = dev->config;

	LOG_HEXDUMP_DBG(buf, len, "TX");

	int64_t deadline = k_uptime_get() + UART_SLIP_SEND_BUDGET_MS;

	if (send_byte(config->uart, SLIP_END, deadline) != 0) {
		return -ETIMEDOUT;
	}

	while (len--) {
		uint8_t b = *buf++;
		int ret;

		switch (b) {
		case SLIP_END:
			ret = send_byte(config->uart, SLIP_ESC, deadline);
			if (ret != 0) {
				return ret;
			}
			ret = send_byte(config->uart, SLIP_ESC_END, deadline);
			break;
		case SLIP_ESC:
			ret = send_byte(config->uart, SLIP_ESC, deadline);
			if (ret != 0) {
				return ret;
			}
			ret = send_byte(config->uart, SLIP_ESC_ESC, deadline);
			break;
		default:
			ret = send_byte(config->uart, b, deadline);
			break;
		}

		if (ret != 0) {
			return ret;
		}
	}

	return send_byte(config->uart, SLIP_END, deadline);
}

int uart_slip_receive(const struct device *dev, uint8_t *buf, uint32_t limit, uint32_t *len)
{
	struct uart_slip_data *data = dev->data;

	uint8_t b;

	*len = 0;
	while (*len <= limit) {
		if (!ring_buf_get(&data->rx_rb, &b, 1)) {
			k_usleep(10);
			continue;
		}

		switch (data->state) {
		case STATE_SKIP: {
			if (b == SLIP_END) {
				data->state = STATE_BYTE;
			}
		} break;
		case STATE_BYTE: {
			if (b == SLIP_ESC) {
				data->state = STATE_ESC;
			} else if (b == SLIP_END) {
				if (*len > 0) {
					LOG_HEXDUMP_DBG(buf, *len, "RX");
					return 0;
				}
			} else {
				*(buf + *len) = b;
				*len += 1;
			}
		} break;
		case STATE_ESC: {
			if (b == SLIP_ESC_END) {
				*(buf + *len) = SLIP_END;
				*len += 1;
				data->state = STATE_BYTE;
			} else if (b == SLIP_ESC_ESC) {
				*(buf + *len) = SLIP_ESC;
				*len += 1;
				data->state = STATE_BYTE;
			} else {
				// this state is unnormal
				// skip all following bytes until next SLIP_END
				data->state = STATE_SKIP;
			}
		} break;
		}

		k_yield();
	}

	return -ENOMEM;
}

static void uart_slip_isr(const struct device *uart, void *user_data)
{
	const struct device *dev = (const struct device *)user_data;
	struct uart_slip_data *data = dev->data;

	int ret;
	uint8_t *buf;
	uint32_t bytes_allocated = 0;
	uint32_t bytes_read = 0;

	while (uart_irq_update(uart) && uart_irq_rx_ready(uart)) {
		if (!bytes_allocated) {
			bytes_allocated = ring_buf_put_claim(&data->rx_rb, &buf, UINT32_MAX);
		}
		if (!bytes_allocated) {
			LOG_ERR("Ring buffer full");
			break;
		}

		ret = uart_fifo_read(uart, buf, bytes_allocated);
		if (ret <= 0) {
			continue;
		}

		buf += ret;
		bytes_read += ret;
		bytes_allocated -= ret;
	}

	ring_buf_put_finish(&data->rx_rb, bytes_read);
}

int uart_slip_init(const struct device *dev)
{
	struct uart_slip_data *data = dev->data;
	const struct uart_slip_config *config = dev->config;

	if (!device_is_ready(config->uart)) {
		LOG_ERR("%s: UART device is not ready: %s", dev->name, config->uart->name);
		return -ENODEV;
	}

	ring_buf_init(&data->rx_rb, sizeof(data->rx_buf), data->rx_buf);

	uart_irq_rx_disable(config->uart);
	uart_irq_tx_disable(config->uart);

	/* Drain the fifo */
	uint8_t c;
	while (uart_fifo_read(config->uart, &c, 1)) {
		continue;
	}

	uart_irq_callback_user_data_set(config->uart, uart_slip_isr, (void *)dev);
	uart_irq_rx_enable(config->uart);

	return 0;
}

#define UART_SLIP_INST(n)                                                                          \
	static struct uart_slip_data uart_slip_data_##n = {                                        \
		.state = STATE_SKIP,                                                               \
	};                                                                                         \
                                                                                                   \
	static const struct uart_slip_config uart_slip_config_##n = {                              \
		.uart = DEVICE_DT_GET(DT_INST_PHANDLE(n, uart)),                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, uart_slip_init, NULL, &uart_slip_data_##n, &uart_slip_config_##n, \
			      POST_KERNEL, CONFIG_UART_SLIP_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(UART_SLIP_INST)
