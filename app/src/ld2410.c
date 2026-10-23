/*
 * ld2410.c — the LD2410 driver: Step 2's parser, behind an interface.
 *
 * Structure (the same layers as Steps 1–2, now owned by the driver):
 *
 *   UART IRQ          drains the hardware FIFO into a ring buffer
 *   parser thread     wakes on a semaphore, drains the ring, feeds
 *                     the state machine, and for every VALID frame:
 *                       1. stores the reading as the latest snapshot
 *                       2. invokes the application callback
 *   application       never sees a byte. It sees readings.
 *
 * The frame format comments live on the parser below. Where the
 * protocol document leaves room for doubt, the code says so.
 */

#include "ld2410.h"

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_uarte.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(ld2410, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Receive path.                                                       */
/* ------------------------------------------------------------------ */

#define LD2410_RING_SIZE 1024		/* power of two (byte ring rule) */
#define LD2410_THREAD_STACK 2048
#define LD2410_THREAD_PRIO 7		/* same tier as the app's helpers */

RING_BUF_DECLARE(ld2410_ring, LD2410_RING_SIZE);
static K_SEM_DEFINE(ld2410_sem, 0, 1);

/* Bytes lost to a full ring, counted in the ISR — the one piece of
 * driver state interrupt context may touch, and atomic precisely
 * because the parser thread folds it into the published stats.
 */
static atomic_t ld2410_isr_dropped;

static const struct device *ld2410_uart;
static ld2410_callback_t ld2410_cb;
static void *ld2410_cb_user;
static bool ld2410_started;

static struct ld2410_stats ld2410_stats_live;	/* driver threads only */
static struct ld2410_stats ld2410_stats_pub;	/* mutex-guarded copy  */
static struct ld2410_reading ld2410_latest_snap;
static bool ld2410_have_reading;
static K_MUTEX_DEFINE(ld2410_lock);

static void ld2410_uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uint8_t chunk[32];

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		int n = uart_fifo_read(dev, chunk, sizeof(chunk));

		if (n <= 0) {
			break;
		}

		uint32_t stored = ring_buf_put(&ld2410_ring, chunk, (uint32_t)n);

		if (stored < (uint32_t)n) {
			/* The one counter written from ISR context:
			 * atomic, and mirrored into the published
			 * stats by the parser thread.
			 */
			atomic_add(&ld2410_isr_dropped, n - (int)stored);
		}

		k_sem_give(&ld2410_sem);
	}
}

/* ------------------------------------------------------------------ */
/* The parser — Step 2's state machine, unchanged in spirit.           */
/*                                                                     */
/* Frame: F4 F3 F2 F1 | len u16 LE | payload[len] | F8 F7 F6 F5        */
/* Basic report payload (len 13):                                      */
/*   [0] 0x02 type · [1] 0xAA · [2] target state · [3..4] move dist    */
/*   LE cm · [5] move energy · [6..7] still dist LE cm · [8] still     */
/*   energy · [9..10] detection dist LE cm · [11] 0x55 · [12] 0x00     */
/* No checksum exists in this protocol; markers + length + footer      */
/* are the validation, and resyncs are counted, never hidden.          */
/* ------------------------------------------------------------------ */

#define LD2410_TYPE_BASIC 0x02
#define LD2410_TYPE_ENGINEERING 0x01
#define LD2410_MAX_PAYLOAD 64

enum parser_state { PS_HUNT, PS_LEN, PS_PAYLOAD, PS_FOOTER };

static const uint8_t header_seq[4] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t footer_seq[4] = { 0xF8, 0xF7, 0xF6, 0xF5 };

static enum parser_state ps_state;
static uint8_t ps_match;
static uint16_t ps_len;
static uint8_t ps_len_bytes;
static uint8_t ps_payload[LD2410_MAX_PAYLOAD];
static uint16_t ps_payload_pos;

static void parser_reset(void)
{
	ps_state = PS_HUNT;
	ps_match = 0;
	ps_len = 0;
	ps_len_bytes = 0;
	ps_payload_pos = 0;
}

static void parser_abort(void)
{
	ld2410_stats_live.resyncs++;
	LOG_WRN("resync #%u (frames ok: %u)",
		ld2410_stats_live.resyncs, ld2410_stats_live.frames_ok);
	parser_reset();
}

static void publish_reading(const struct ld2410_reading *r)
{
	k_mutex_lock(&ld2410_lock, K_FOREVER);
	ld2410_latest_snap = *r;
	ld2410_have_reading = true;
	ld2410_stats_live.frames_ok++;
	ld2410_stats_pub = ld2410_stats_live;
	k_mutex_unlock(&ld2410_lock);

	LOG_DBG("frame #%u: target %d, move %u cm (e%u), still %u cm (e%u)",
		ld2410_stats_live.frames_ok, r->target,
		r->move_distance_cm, r->move_energy,
		r->still_distance_cm, r->still_energy);

	if (ld2410_cb != NULL) {
		ld2410_cb(r, ld2410_cb_user);
	}
}

static void handle_payload(void)
{
	if (ps_payload[0] == LD2410_TYPE_ENGINEERING) {
		/* Documented mode, deliberately not decoded in
		 * this course. Counted, and mirrored to the
		 * published stats so the shell can show it.
		 */
		ld2410_stats_live.eng_frames++;
		k_mutex_lock(&ld2410_lock, K_FOREVER);
		ld2410_stats_pub = ld2410_stats_live;
		k_mutex_unlock(&ld2410_lock);
		return;
	}

	if (ps_payload[0] != LD2410_TYPE_BASIC || ps_len != 13 ||
	    ps_payload[1] != 0xAA || ps_payload[11] != 0x55 ||
	    ps_payload[12] != 0x00) {
		parser_abort();
		return;
	}

	struct ld2410_reading r = {
		.target = (enum ld2410_target)ps_payload[2],
		.move_distance_cm = sys_get_le16(&ps_payload[3]),
		.move_energy = ps_payload[5],
		.still_distance_cm = sys_get_le16(&ps_payload[6]),
		.still_energy = ps_payload[8],
		.detect_distance_cm = sys_get_le16(&ps_payload[9]),
		.timestamp_ms = k_uptime_get(),
	};

	publish_reading(&r);
}

static void parser_feed(uint8_t byte)
{
	switch (ps_state) {
	case PS_HUNT:
		if (byte == header_seq[ps_match]) {
			ps_match++;
			if (ps_match == sizeof(header_seq)) {
				ps_state = PS_LEN;
				ps_match = 0;
				ps_len = 0;
				ps_len_bytes = 0;
			}
		} else {
			ps_match = (byte == header_seq[0]) ? 1 : 0;
		}
		break;

	case PS_LEN:
		ps_len |= (uint16_t)byte << (8 * ps_len_bytes);
		ps_len_bytes++;
		if (ps_len_bytes == 2) {
			if (ps_len < 3 || ps_len > LD2410_MAX_PAYLOAD) {
				parser_abort();
			} else {
				ps_state = PS_PAYLOAD;
				ps_payload_pos = 0;
			}
		}
		break;

	case PS_PAYLOAD:
		ps_payload[ps_payload_pos++] = byte;
		if (ps_payload_pos >= ps_len) {
			ps_state = PS_FOOTER;
			ps_match = 0;
		}
		break;

	case PS_FOOTER:
		if (byte == footer_seq[ps_match]) {
			ps_match++;
			if (ps_match == sizeof(footer_seq)) {
				handle_payload();
				parser_reset();
			}
		} else {
			parser_abort();
			if (byte == header_seq[0]) {
				ps_state = PS_HUNT;
				ps_match = 1;
			}
		}
		break;
	}
}

/* ------------------------------------------------------------------ */
/* The parser thread — the driver's engine room.                       */
/* ------------------------------------------------------------------ */

static void ld2410_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t bytes[64];

	while (1) {
		k_sem_take(&ld2410_sem, K_FOREVER);

		/* Fold any ISR-side drops into the published stats
		 * here, in thread context, where the mutex is legal.
		 */
		long dropped = atomic_set(&ld2410_isr_dropped, 0);

		if (dropped != 0) {
			k_mutex_lock(&ld2410_lock, K_FOREVER);
			ld2410_stats_live.rx_dropped += (uint32_t)dropped;
			ld2410_stats_pub = ld2410_stats_live;
			k_mutex_unlock(&ld2410_lock);
		}

		uint32_t n;

		while ((n = ring_buf_get(&ld2410_ring, bytes, sizeof(bytes))) > 0) {
			for (uint32_t i = 0; i < n; i++) {
				parser_feed(bytes[i]);
			}
		}
	}
}

K_THREAD_DEFINE(ld2410_thread_id, LD2410_THREAD_STACK, ld2410_thread,
		NULL, NULL, NULL, LD2410_THREAD_PRIO, 0, 0);

/* ------------------------------------------------------------------ */
/* The public interface (the contract is in ld2410.h).                 */
/* ------------------------------------------------------------------ */

int ld2410_start(const struct device *uart, ld2410_callback_t cb,
		 void *user_data)
{
	if (ld2410_started) {
		return -EALREADY;
	}
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	ld2410_uart = uart;
	ld2410_cb = cb;
	ld2410_cb_user = user_data;

	parser_reset();

	/*
	 * The module's 256,000 baud exists in NEITHER of Zephyr's
	 * baud tables for this chip — the devicetree binding
	 * rejects it and the UARTE driver's lookup returns
	 * -EINVAL (Step 1 tells the whole story). So the driver
	 * writes the divider register directly, from the
	 * nRF52840 datasheet formula:
	 *
	 *     BAUDRATE = baud * 2^32 / 16,000,000  (peripheral clock)
	 *
	 * giving an actual rate of 255,999.997. The `uart` device
	 * handed in is uart1 (see the application); its register
	 * block comes from that same devicetree node. Owning this
	 * quirk HERE — one place, commented — is what makes it a
	 * driver detail instead of an application bug.
	 */
	nrf_uarte_baudrate_set(
		(NRF_UARTE_Type *)DT_REG_ADDR(DT_NODELABEL(uart1)),
		(nrf_uarte_baudrate_t)((256000ULL << 32) / 16000000ULL));

	uart_irq_callback_user_data_set(ld2410_uart, ld2410_uart_isr, NULL);
	uart_irq_rx_enable(ld2410_uart);

	ld2410_started = true;

	LOG_INF("LD2410 driver started (basic reporting mode expected)");

	return 0;
}

bool ld2410_latest(struct ld2410_reading *out)
{
	k_mutex_lock(&ld2410_lock, K_FOREVER);
	bool have = ld2410_have_reading;

	if (have && out != NULL) {
		*out = ld2410_latest_snap;
	}
	k_mutex_unlock(&ld2410_lock);

	return have;
}

void ld2410_stats(struct ld2410_stats *out)
{
	k_mutex_lock(&ld2410_lock, K_FOREVER);
	if (out != NULL) {
		*out = ld2410_stats_pub;
	}
	k_mutex_unlock(&ld2410_lock);
}

uint16_t ld2410_headline_distance_cm(const struct ld2410_reading *r)
{
	if (r == NULL) {
		return 0;
	}

	switch (r->target) {
	case LD2410_TARGET_STATIONARY:
	case LD2410_TARGET_BOTH:
		return r->still_distance_cm;
	case LD2410_TARGET_MOVING:
		return r->move_distance_cm;
	default:
		return 0;
	}
}
