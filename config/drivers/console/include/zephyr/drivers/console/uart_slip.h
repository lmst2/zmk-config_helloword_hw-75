/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * @brief A transport layer built on top of UART
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Encode and send data over UART
 *
 * @param[in] dev   Device instance
 * @param[in] buf   Pointer to data to send
 * @param[in] len   Number of bytes to send
 *
 * @return 0 on success or negative error
 */
int uart_slip_send(const struct device *dev, const uint8_t *buf, uint32_t len);

/**
 * @brief Receive and decode data from UART
 *
 * @param[in] dev   Device instance
 * @param[in] buf   Pointer to data buffer to write to
 * @param[in] limit Max length of data to receive
 * @param[out] len  Number of bytes received
 *
 * @return 0 on success or negative error
 */
int uart_slip_receive(const struct device *dev, uint8_t *buf, uint32_t limit, uint32_t *len);

/**
 * @brief Non-blocking receive: drain whatever is in the RX ring buffer through
 * the SLIP state machine, appending decoded bytes to @p buf. @p buf and @p len
 * must persist across calls so a frame can span polls (the caller keeps them
 * static and resets *len to 0 after processing a frame).
 *
 * @return 0 when a complete frame is ready in @p buf, -EAGAIN when no more bytes
 *         are available right now, or -ENOMEM if the frame exceeds @p limit.
 */
int uart_slip_receive_nb(const struct device *dev, uint8_t *buf, uint32_t limit, uint32_t *len);

#ifdef __cplusplus
}
#endif
