/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(usb_comm, CONFIG_HW75_USB_COMM_LOG_LEVEL);

#include <zephyr/usb/usb_device.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include <app/diag_log.h>

#include "usb_comm_hid.h"
#include "usb_comm.pb.h"

#include "handler/handler.h"

static struct k_sem usb_comm_sem;

static K_THREAD_STACK_DEFINE(usb_comm_thread_stack, CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE);
static struct k_thread usb_comm_thread;

static uint32_t usb_rx_idx, usb_rx_len;
static uint8_t usb_rx_buf[CONFIG_HW75_USB_COMM_MAX_RX_MESSAGE_SIZE];
static uint8_t usb_tx_buf[CONFIG_HW75_USB_COMM_MAX_TX_MESSAGE_SIZE];
/*
 * USB comm requests are handled on a single worker thread. Reusing one H2D/D2H
 * message pair avoids reserving the full protobuf union payloads on the 1 KB
 * thread stack for every request.
 */
static usb_comm_MessageH2D usb_h2d_msg;
static usb_comm_MessageD2H usb_d2h_msg;

static uint8_t bytes_field[CONFIG_HW75_USB_COMM_MAX_BYTES_FIELD_SIZE];
static uint32_t bytes_field_len = 0;

static void usb_comm_log_stack_watermark(uint16_t action, uint16_t stage) {
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
	static const uint8_t thresholds[] = {50U, 70U, 90U, 100U};
	static uint8_t threshold_index;
	size_t unused = 0U;
	size_t used = 0U;
	size_t used_pct = 0U;

	if (k_thread_stack_space_get(k_current_get(), &unused) != 0) {
		return;
	}

	unused = MIN(unused, (size_t)CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE);
	used = (size_t)CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE - unused;
	used_pct = (used * 100U) / CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE;

	while (threshold_index < (sizeof(thresholds) / sizeof(thresholds[0])) &&
	       used_pct >= thresholds[threshold_index]) {
		uint16_t stage_and_threshold =
			(uint16_t)stage | ((uint16_t)thresholds[threshold_index] << 8);

		hw75_diag_log_event(HW75_DIAG_LEVEL_WARN, HW75_DIAG_MODULE_USB_COMM, 0U,
				    HW75_DIAG_EVENT_USB_STACK_WATERMARK,
				    hw75_diag_pack_u16x2(action, (uint16_t)unused), true,
				    hw75_diag_pack_u16x2(
					    CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE,
					    stage_and_threshold));
		threshold_index++;
	}
#else
	ARG_UNUSED(action);
	ARG_UNUSED(stage);
#endif
}

#if CONFIG_HW75_USB_COMM_MAX_BYTES_FIELD_SIZE
static bool read_bytes_field(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(field);
	ARG_UNUSED(arg);

	if (stream->bytes_left > sizeof(bytes_field)) {
		LOG_ERR("Buffer overflows decoding %d bytes", stream->bytes_left);
		hw75_diag_log_event(HW75_DIAG_LEVEL_ERROR, HW75_DIAG_MODULE_USB_COMM, 0U,
				    HW75_DIAG_EVENT_USB_BYTES_OVERFLOW,
				    hw75_diag_pack_u16x2((uint16_t)stream->bytes_left,
							 (uint16_t)sizeof(bytes_field)),
				    false, 0U);
		return false;
	}

	uint32_t bytes_len = stream->bytes_left;

	if (!pb_read(stream, bytes_field, stream->bytes_left)) {
		LOG_ERR("Failed decoding bytes: %s", stream->errmsg);
		return false;
	}

	bytes_field_len = bytes_len;
	LOG_DBG("Decoded %d bytes", bytes_field_len);

	return true;
}
#endif

#if defined(CONFIG_HW75_EINK_MODES)
extern void usb_comm_eink_mode_prepare_decode(usb_comm_EinkModeConfig *cfg);
#endif
#if defined(CONFIG_HW75_TOUCHBAR)
extern void usb_comm_touchbar_prepare_decode(usb_comm_TouchbarConfig *cfg);
#endif
#if defined(CONFIG_HW75_FUNCTION_SLOT)
extern void usb_comm_function_slot_prepare_decode(usb_comm_FunctionSlotConfig *cfg);
#endif

/*
 * MessageH2D has `submsg_callback = true`, which generates a cb_payload hook
 * that fires when pb_decode is about to enter one of the oneof submessages.
 * We use that hook to wire per-field decode callbacks for oneof members that
 * were marked FT_CALLBACK (to keep the oneof union small). The hook itself
 * is always compiled; its individual branches are guarded by the feature
 * they serve so boards that never handle a given action pay no flash for it.
 */
static bool h2d_callback(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	ARG_UNUSED(stream);
	ARG_UNUSED(arg);
#if CONFIG_HW75_USB_COMM_MAX_BYTES_FIELD_SIZE
	if (field->tag == usb_comm_MessageH2D_eink_image_tag) {
		usb_comm_EinkImage *eink_image = field->pData;
		eink_image->bits.funcs.decode = read_bytes_field;
	}
	if (field->tag == usb_comm_MessageH2D_eink_frame_tag) {
		usb_comm_EinkFrame *eink_frame = field->pData;
		eink_frame->bits.funcs.decode = read_bytes_field;
	}
#endif
#if defined(CONFIG_HW75_EINK_MODES)
	if (field->tag == usb_comm_MessageH2D_eink_mode_config_tag) {
		usb_comm_EinkModeConfig *cfg = field->pData;
		usb_comm_eink_mode_prepare_decode(cfg);
	}
#endif
#if defined(CONFIG_HW75_TOUCHBAR)
	if (field->tag == usb_comm_MessageH2D_touchbar_config_tag) {
		usb_comm_TouchbarConfig *cfg = field->pData;
		usb_comm_touchbar_prepare_decode(cfg);
	}
#endif
#if defined(CONFIG_HW75_FUNCTION_SLOT)
	if (field->tag == usb_comm_MessageH2D_function_slot_config_tag) {
		usb_comm_FunctionSlotConfig *cfg = field->pData;
		usb_comm_function_slot_prepare_decode(cfg);
	}
#endif
	return true;
}

static void usb_comm_handle_message()
{
	LOG_DBG("message size %u", usb_rx_len);
	LOG_HEXDUMP_DBG(usb_rx_buf, MIN(usb_rx_len, 64), "message data");

	pb_istream_t h2d_stream = pb_istream_from_buffer(usb_rx_buf, usb_rx_len);
	pb_ostream_t d2h_stream = pb_ostream_from_buffer(usb_tx_buf, sizeof(usb_tx_buf));
	usb_comm_MessageH2D *h2d = &usb_h2d_msg;
	usb_comm_MessageD2H *d2h = &usb_d2h_msg;

	*h2d = (usb_comm_MessageH2D)usb_comm_MessageH2D_init_zero;
	*d2h = (usb_comm_MessageD2H)usb_comm_MessageD2H_init_zero;

#if CONFIG_HW75_USB_COMM_MAX_BYTES_FIELD_SIZE
	/*
	 * Reset the streamed bytes length per message. read_bytes_field() only
	 * runs when an eink_image/eink_frame actually carries its optional `bits`;
	 * without this reset, a message that omits `bits` would reuse the previous
	 * frame's length over stale buffer contents.
	 */
	bytes_field_len = 0;
#endif

	h2d->cb_payload.funcs.decode = h2d_callback;

	if (!pb_decode_delimited(&h2d_stream, usb_comm_MessageH2D_fields, h2d)) {
		LOG_ERR("Failed decoding h2d message: %s", h2d_stream.errmsg);
		/* Thread context (same as the stack-watermark diag below), so logging
		 * here is safe; surfaces a malformed request to the host Debug page. */
		hw75_diag_log_event(HW75_DIAG_LEVEL_ERROR, HW75_DIAG_MODULE_USB_COMM, 0U,
				    HW75_DIAG_EVENT_USB_DECODE_FAIL, usb_rx_len, false, 0U);
		return;
	}

	LOG_DBG("req action: %d", h2d->action);
	d2h->action = h2d->action;
	d2h->which_payload = usb_comm_MessageD2H_nop_tag;
	usb_comm_log_stack_watermark((uint16_t)h2d->action, 1U);

	STRUCT_SECTION_FOREACH(usb_comm_handler_config, config)
	{
		if (config->action == h2d->action) {
			if (config->handler(h2d, d2h, bytes_field, bytes_field_len)) {
				d2h->which_payload = config->response_payload;
			}
			break;
		}
	}
	usb_comm_log_stack_watermark((uint16_t)h2d->action, 2U);

	if (!pb_encode_delimited(&d2h_stream, usb_comm_MessageD2H_fields, d2h)) {
		LOG_ERR("Failed encoding d2h message: %s", d2h_stream.errmsg);
		hw75_diag_log_event(HW75_DIAG_LEVEL_ERROR, HW75_DIAG_MODULE_USB_COMM, 0U,
				    HW75_DIAG_EVENT_USB_ENCODE_FAIL, (uint32_t)d2h->action, false,
				    0U);
		return;
	}
	usb_comm_log_stack_watermark((uint16_t)h2d->action, 3U);

	int send_ret = usb_comm_hid_send(usb_tx_buf, d2h_stream.bytes_written);
	if (send_ret != 0) {
		LOG_ERR("Failed sending response for action %d: %d", d2h->action, send_ret);
	}
}

static void usb_comm_handle_packet(uint8_t *data, uint32_t len)
{
	if (usb_rx_idx + len > sizeof(usb_rx_buf)) {
		LOG_ERR("RX buffer overflows, index: %d, received: %d", usb_rx_idx, len);
		usb_rx_idx = 0;
		return;
	}

	if (data[0] + 1 > len) {
		LOG_ERR("Invalid packet header: %d, len: %d", data[0], len);
		return;
	}

	memcpy(usb_rx_buf + usb_rx_idx, data + 1, data[0]);
	usb_rx_idx += data[0];

	if (data[0] + 1 < len) {
		usb_rx_len = usb_rx_idx;
		usb_rx_idx = 0;
		k_sem_give(&usb_comm_sem);
	}
}

static void usb_comm_thread_entry(void *p1, void *p2, void *p3)
{
	usb_comm_hid_init(usb_comm_handle_packet);
	while (true) {
		k_sem_take(&usb_comm_sem, K_FOREVER);
		usb_comm_handle_message();
	}
}

static int usb_comm_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	k_sem_init(&usb_comm_sem, 0, 1);

	k_thread_create(&usb_comm_thread, usb_comm_thread_stack,
			CONFIG_HW75_USB_COMM_THREAD_STACK_SIZE, usb_comm_thread_entry, NULL, NULL,
			NULL, K_PRIO_COOP(CONFIG_HW75_USB_COMM_THREAD_PRIORITY), 0, K_NO_WAIT);

	return 0;
}

SYS_INIT(usb_comm_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
