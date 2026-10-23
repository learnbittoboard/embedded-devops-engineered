/*
 * ld2410_parser.h — the LD2410 frame parser as a pure module.
 *
 * Chapter 6 of Embedded DevOps, Engineered extracted this state
 * machine out of the UART-bound driver (Course 1's ld2410.c) so it
 * can be unit-tested on a host: no Zephyr headers, no threads, no
 * device — bytes in, decoded reports out. The driver keeps a parser
 * instance and feeds it from its ring buffer; the tests feed it from
 * arrays. Same code, two hosts.
 *
 * Frame: F4 F3 F2 F1 | len u16 LE | payload[len] | F8 F7 F6 F5
 * Basic report payload (len 13):
 *   [0] 0x02 type · [1] 0xAA · [2] target state · [3..4] move dist
 *   LE cm · [5] move energy · [6..7] still dist LE cm · [8] still
 *   energy · [9..10] detection dist LE cm · [11] 0x55 · [12] 0x00
 * No checksum exists in this protocol; markers + length + footer
 * are the validation, and resyncs are counted, never hidden.
 */

#ifndef LD2410_PARSER_H_
#define LD2410_PARSER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LD2410_TYPE_BASIC       0x02
#define LD2410_TYPE_ENGINEERING 0x01
#define LD2410_MAX_PAYLOAD      64

/* Target state, exactly as the module reports it. Values match
 * enum ld2410_target in ld2410.h — the driver converts by cast.
 */
enum ld2410_parser_target {
	LD2410_PARSER_TARGET_NONE = 0,
	LD2410_PARSER_TARGET_MOVING = 1,
	LD2410_PARSER_TARGET_STATIONARY = 2,
	LD2410_PARSER_TARGET_BOTH = 3,
};

/* One decoded basic report. No timestamp: when a frame arrived is
 * the caller's business (the driver stamps k_uptime_get()); what
 * the frame SAID is the parser's.
 */
struct ld2410_report {
	enum ld2410_parser_target target;
	uint16_t move_distance_cm;
	uint8_t move_energy;
	uint16_t still_distance_cm;
	uint8_t still_energy;
	uint16_t detect_distance_cm;
};

/* Parser instance. Stack-allocatable, no hidden globals: two
 * parsers can run side by side (the tests do exactly that).
 */
struct ld2410_parser {
	uint8_t state;     /* enum parser_state (private) */
	uint8_t match;     /* header/footer match progress  */
	uint16_t len;      /* declared payload length       */
	uint8_t len_bytes; /* length bytes consumed so far  */
	uint8_t payload[LD2410_MAX_PAYLOAD];
	uint16_t payload_pos;

	/* The honesty counters, same meanings as the driver's:
	 * valid basic reports, abandoned sequences, engineering
	 * frames seen (counted, not decoded).
	 */
	uint32_t frames_ok;
	uint32_t resyncs;
	uint32_t eng_frames;
};

void ld2410_parser_init(struct ld2410_parser *p);

/*
 * Feed one byte. Returns true exactly when this byte completed a
 * VALID basic report, which is then stored in *out (out may be
 * NULL if only the counters matter). Engineering frames, resyncs
 * and partial frames all return false; the counters say which.
 */
bool ld2410_parser_feed(struct ld2410_parser *p, uint8_t byte, struct ld2410_report *out);

/*
 * The product's favourite question: how far away is the person?
 * Stationary wins when present (a seated person is the occupancy
 * case), else the moving target, else 0 for nobody.
 */
uint16_t ld2410_report_headline_cm(const struct ld2410_report *r);

#ifdef __cplusplus
}
#endif

#endif /* LD2410_PARSER_H_ */
