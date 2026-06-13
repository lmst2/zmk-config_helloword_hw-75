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

/* ---- Reverse channel RX: dynamic -> keyboard (F6) ----
 *
 * The keyboard cannot afford a resident blocking RX thread (the F103 has ~200 B
 * SRAM margin), so instead of uart_slip_receive() (which blocks) we drain the
 * ISR-filled ring buffer non-blocking on the system workqueue. D2K frames are
 * tiny and human-paced, so a 15 ms poll is plenty.
 */
#include <zephyr/init.h>
#include <pb_decode.h>
#include <zmk/rgb_underglow.h>

#define UART_RX_POLL_INTERVAL_MS 15

static uint8_t uart_rx_frame[CONFIG_HW75_UART_COMM_MAX_TX_MESSAGE_SIZE];
static uint32_t uart_rx_frame_len;
static struct k_work_delayable uart_rx_poll_work;

static void uart_comm_handle_d2k(const uart_comm_MessageD2K *d2k)
{
	if (d2k->action == uart_comm_ActionD2K_D2K_RGB &&
	    d2k->which_payload == uart_comm_MessageD2K_rgb_tag) {
		switch (d2k->payload.rgb.command) {
		case 1:
			zmk_rgb_underglow_toggle();
			break;
		case 2:
			zmk_rgb_underglow_change_brt(1);
			break;
		case 3:
			zmk_rgb_underglow_change_brt(-1);
			break;
		case 4:
			zmk_rgb_underglow_cycle_effect(1);
			break;
		case 5:
			zmk_rgb_underglow_cycle_effect(-1);
			break;
		default:
			break;
		}
	}
}

static void uart_rx_poll_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int ret;
	while ((ret = uart_slip_receive_nb(slip, uart_rx_frame, sizeof(uart_rx_frame),
					   &uart_rx_frame_len)) == 0) {
		pb_istream_t stream = pb_istream_from_buffer(uart_rx_frame, uart_rx_frame_len);
		uart_comm_MessageD2K d2k = uart_comm_MessageD2K_init_zero;
		if (pb_decode_delimited(&stream, uart_comm_MessageD2K_fields, &d2k)) {
			uart_comm_handle_d2k(&d2k);
		}
		uart_rx_frame_len = 0;
	}

	k_work_reschedule(&uart_rx_poll_work, K_MSEC(UART_RX_POLL_INTERVAL_MS));
}

static int uart_comm_rx_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	uart_rx_frame_len = 0;
	k_work_init_delayable(&uart_rx_poll_work, uart_rx_poll_handler);
	k_work_reschedule(&uart_rx_poll_work, K_MSEC(100));

	return 0;
}

SYS_INIT(uart_comm_rx_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
