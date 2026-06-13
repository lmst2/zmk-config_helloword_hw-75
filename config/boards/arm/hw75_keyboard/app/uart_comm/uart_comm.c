/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/drivers/console/uart_slip.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_comm, CONFIG_HW75_UART_COMM_LOG_LEVEL);

#include <pb_encode.h>

#include "report/report.h"

#define SLIP_NODE DT_ALIAS(uart_comm)

/*
 * Circuit breaker: uart_slip_send already aborts after UART_SLIP_SEND_BUDGET_MS
 * (20 ms) when the peer pins the TX line, but even a 20 ms stall on the
 * cooperative system workqueue is noticeable if it happens often. If we see a
 * few consecutive timeouts we assume the dynamic is offline and stop calling
 * the transport for a while. The next successful report resets the breaker.
 */
#define UART_COMM_MAX_CONSECUTIVE_FAILURES 3
#define UART_COMM_BACKOFF_MS (5 * 60 * 1000)

static uint8_t uart_tx_buf[CONFIG_HW75_UART_COMM_MAX_TX_MESSAGE_SIZE];

static const struct device *slip = DEVICE_DT_GET(SLIP_NODE);

static uint32_t uart_failure_streak;
static int64_t uart_suspend_until_ms;

bool uart_comm_report(uart_comm_MessageK2D *k2d)
{
	LOG_DBG("report action: %d", k2d->action);

	int64_t now = k_uptime_get();
	if (now < uart_suspend_until_ms) {
		return false;
	}

	pb_ostream_t k2d_stream = pb_ostream_from_buffer(uart_tx_buf, sizeof(uart_tx_buf));

	if (!pb_encode_delimited(&k2d_stream, uart_comm_MessageK2D_fields, k2d)) {
		LOG_ERR("Failed encoding k2d message: %s", k2d_stream.errmsg);
		return false;
	}

	int ret = uart_slip_send(slip, uart_tx_buf, k2d_stream.bytes_written);
	if (ret != 0) {
		uart_failure_streak++;
		if (uart_failure_streak >= UART_COMM_MAX_CONSECUTIVE_FAILURES) {
			uart_suspend_until_ms = k_uptime_get() + UART_COMM_BACKOFF_MS;
			LOG_WRN("uart_slip stalled %u times in a row (last err %d); "
				"suspending reports for %u ms",
				uart_failure_streak, ret, UART_COMM_BACKOFF_MS);
		}
		return false;
	}

	uart_failure_streak = 0;
	return true;
}
