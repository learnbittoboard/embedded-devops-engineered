/*
 * ld2410.h — a small driver for the HLK-LD2410 mmWave sensor.
 *
 * THE CONTRACT (everything application code is allowed to know):
 *
 *   - Call ld2410_start() once with the sensor's UART device.
 *     From then on, the driver runs itself: its own interrupt,
 *     its own parser thread, its own counters.
 *   - Every VALID frame invokes your callback — or, ignore the
 *     callback (pass NULL) and poll ld2410_latest() instead.
 *   - The callback runs in the DRIVER'S parser thread context.
 *     Keep it short: copy the reading, submit a work item
 *     (Chapter 2), get out. The example app does exactly that.
 *   - A reading is a snapshot struct, copied by value. No
 *     pointers into driver internals ever escape.
 *   - ld2410_stats() exposes the honesty counters: frames
 *     parsed, resyncs, dropped ring bytes. If the wiring is
 *     bad, these numbers say so before your users do.
 *
 * What the driver hides, on purpose: bytes, frames, the ring
 * buffer, the state machine, the UART API. Chapter 7's Zigbee
 * endpoint will consume THIS interface — it will never see a
 * byte, and that is the point of a driver.
 */

#ifndef LD2410_H_
#define LD2410_H_

#include <zephyr/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Target state, exactly as the module reports it. */
enum ld2410_target {
	LD2410_TARGET_NONE = 0,		/* nobody detected            */
	LD2410_TARGET_MOVING = 1,	/* a moving target only       */
	LD2410_TARGET_STATIONARY = 2,	/* a still target only        */
	LD2410_TARGET_BOTH = 3,		/* moving AND still targets   */
};

/* One parsed presence report. Distances in centimetres,
 * energies 0..100 as the module scales them.
 */
struct ld2410_reading {
	enum ld2410_target target;
	uint16_t move_distance_cm;
	uint8_t move_energy;
	uint16_t still_distance_cm;
	uint8_t still_energy;
	uint16_t detect_distance_cm;	/* the module's max-of-two */
	int64_t timestamp_ms;		/* k_uptime_get() at parse time */
};

struct ld2410_stats {
	uint32_t frames_ok;		/* valid basic reports parsed */
	uint32_t resyncs;		/* sequences abandoned        */
	uint32_t eng_frames;		/* engineering-mode frames (counted, not decoded) */
	uint32_t rx_dropped;		/* bytes lost to a full ring buffer */
};

/*
 * Called from the driver's parser thread for every valid frame.
 * `reading` is valid only for the duration of the call — copy
 * what you need (it is a small struct; copy the whole thing).
 */
typedef void (*ld2410_callback_t)(const struct ld2410_reading *reading,
				  void *user_data);

/*
 * Start the driver on an already-enabled UART device (see the
 * step's app.overlay — the device must be uart1). The driver
 * sets the port to the module's documented 256,000 baud itself:
 * that rate is missing from Zephyr's baud tables for this chip,
 * so ld2410.c writes the UARTE divider register from the
 * datasheet formula. cb may be NULL if you only poll
 * ld2410_latest(). Returns 0, or a negative errno if the UART
 * is not ready. Calling twice is an error (-EALREADY): one
 * sensor, one driver instance — this module is deliberately a
 * singleton, like the hardware it serves.
 */
int ld2410_start(const struct device *uart, ld2410_callback_t cb,
		 void *user_data);

/* Copy out the most recent valid reading. Returns false (and
 * touches nothing) if no valid frame has arrived yet.
 */
bool ld2410_latest(struct ld2410_reading *out);

/* Copy out the counters. Always succeeds. */
void ld2410_stats(struct ld2410_stats *out);

/*
 * The one question the product asks most: how far away is the
 * person? Stationary wins when present (a seated person is the
 * occupancy case), else the moving target, else 0 for nobody.
 * Chapter 7 turns this into an attribute; here it is a function.
 */
uint16_t ld2410_headline_distance_cm(const struct ld2410_reading *r);

#ifdef __cplusplus
}
#endif

#endif /* LD2410_H_ */
