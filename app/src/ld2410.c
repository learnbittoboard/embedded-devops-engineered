/*
 * ld2410.c — the LD2410 driver, refactored in Chapter 6 of
 * Embedded DevOps, Engineered.
 *
 * What changed and what did not: the frame parser STATE MACHINE
 * moved out — it is now ld2410_parser.c, a pure module with no
 * Zephyr in it, unit-tested on a host (app/tests/ld2410_parser).
 * Everything else is Course 1's driver, untouched in behaviour:
 *
 *   UART IRQ          drains the hardware FIFO into a ring buffer
 *   parser thread     wakes on a semaphore, drains the ring, feeds
 *                     the bytes to the parser module, and for every
 *                     VALID frame the module decodes:
 *                       1. stamps it with k_uptime_get()
 *                       2. stores it as the latest snapshot
 *                       3. invokes the application callback
 *   application       never sees a byte. It sees readings.
 *
 * The public contract in ld2410.h is unchanged — Chapter 7's
 * endpoint and the shell consume this driver exactly as before.
 * A refactor that changes the interface is not a refactor; it is
 * a rewrite wearing a costume.
 */

#include "ld2410.h"
#include "ld2410_parser.h"

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_uarte.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(ld2410, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Receive path.                                                       */
/* ------------------------------------------------------------------ */

#define LD2410_RING_SIZE    1024 /* power of two (byte ring rule) */
#define LD2410_THREAD_STACK 2048
#define LD2410_THREAD_PRIO  7 /* same tier as the app's helpers */

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

/* The parser module instance. Its counters (frames_ok, resyncs,
 * eng_frames) are the published stats' source; rx_dropped stays
 * the driver's own, folded in below.
 */
static struct ld2410_parser ld2410_parser;

static struct ld2410_stats ld2410_stats_pub; /* mutex-guarded copy  */
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

/* Mirror the parser module's counters (+ the ISR drop count) into
 * the mutex-guarded published copy the shell and the app read.
 * Parser-thread context only.
 */
static void stats_sync(void)
{
	long dropped = atomic_set(&ld2410_isr_dropped, 0);

	k_mutex_lock(&ld2410_lock, K_FOREVER);
	ld2410_stats_pub.frames_ok = ld2410_parser.frames_ok;
	ld2410_stats_pub.resyncs = ld2410_parser.resyncs;
	ld2410_stats_pub.eng_frames = ld2410_parser.eng_frames;
	ld2410_stats_pub.rx_dropped += (uint32_t)dropped;
	k_mutex_unlock(&ld2410_lock);
}

static void publish_reading(const struct ld2410_report *rep)
{
	struct ld2410_reading r = {
		.target = (enum ld2410_target)rep->target,
		.move_distance_cm = rep->move_distance_cm,
		.move_energy = rep->move_energy,
		.still_distance_cm = rep->still_distance_cm,
		.still_energy = rep->still_energy,
		.detect_distance_cm = rep->detect_distance_cm,
		.timestamp_ms = k_uptime_get(),
	};

	k_mutex_lock(&ld2410_lock, K_FOREVER);
	ld2410_latest_snap = r;
	ld2410_have_reading = true;
	k_mutex_unlock(&ld2410_lock);

	LOG_DBG("frame: target %d, move %u cm (e%u), still %u cm (e%u)", r.target,
		r.move_distance_cm, r.move_energy, r.still_distance_cm, r.still_energy);

	if (ld2410_cb != NULL) {
		ld2410_cb(&r, ld2410_cb_user);
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

		uint32_t n;

		while ((n = ring_buf_get(&ld2410_ring, bytes, sizeof(bytes))) > 0) {
			for (uint32_t i = 0; i < n; i++) {
				struct ld2410_report rep;

				if (ld2410_parser_feed(&ld2410_parser, bytes[i], &rep)) {
					publish_reading(&rep);
				}
			}
		}

		stats_sync();
	}
}

K_THREAD_DEFINE(ld2410_thread_id, LD2410_THREAD_STACK, ld2410_thread, NULL, NULL, NULL,
		LD2410_THREAD_PRIO, 0, 0);

/* ------------------------------------------------------------------ */
/* The public interface (the contract is in ld2410.h).                 */
/* ------------------------------------------------------------------ */

int ld2410_start(const struct device *uart, ld2410_callback_t cb, void *user_data)
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

	ld2410_parser_init(&ld2410_parser);

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
	nrf_uarte_baudrate_set((NRF_UARTE_Type *)DT_REG_ADDR(DT_NODELABEL(uart1)),
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

	/* The rule itself lives in the parser module now (it is
	 * report logic, not driver logic); convert and delegate.
	 */
	struct ld2410_report rep = {
		.target = (enum ld2410_parser_target)r->target,
		.move_distance_cm = r->move_distance_cm,
		.move_energy = r->move_energy,
		.still_distance_cm = r->still_distance_cm,
		.still_energy = r->still_energy,
		.detect_distance_cm = r->detect_distance_cm,
	};

	return ld2410_report_headline_cm(&rep);
}
