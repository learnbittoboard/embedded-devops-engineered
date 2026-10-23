/*
 * Unit tests for the extracted LD2410 frame parser
 * (app/src/ld2410_parser.c) — Chapter 6 of Embedded DevOps,
 * Engineered. Runs on native_sim: no UART, no sensor, just bytes.
 *
 * Every test feeds byte streams the way the real wire delivers
 * them — garbage, truncation and all — and checks both the decoded
 * reports AND the honesty counters, because on the real product
 * the counters are how you know the wiring is lying to you.
 */

#include <zephyr/ztest.h>

#include <string.h>

#include "ld2410_parser.h"

/* A known-good basic report: target BOTH, moving 150 cm @ 80,
 * stationary 200 cm @ 65, detection distance 200 cm.
 */
static const uint8_t basic_payload[13] = {
	0x02, 0xAA, 0x03, 150, 0, /* move distance LE  */
	80,                       /* move energy       */
	200,  0,                  /* still distance LE */
	65,                       /* still energy      */
	200,  0,                  /* detect distance   */
	0x55, 0x00,
};

static size_t build_frame(uint8_t *out, const uint8_t *payload, uint16_t len)
{
	out[0] = 0xF4;
	out[1] = 0xF3;
	out[2] = 0xF2;
	out[3] = 0xF1;
	out[4] = (uint8_t)(len & 0xFF);
	out[5] = (uint8_t)(len >> 8);
	memcpy(out + 6, payload, len);
	out[6 + len] = 0xF8;
	out[7 + len] = 0xF7;
	out[8 + len] = 0xF6;
	out[9 + len] = 0xF5;
	return (size_t)len + 10;
}

/* Feed a whole buffer; returns how many valid reports decoded,
 * with the last one in *last (if non-NULL).
 */
static int feed_all(struct ld2410_parser *p, const uint8_t *buf, size_t n,
		    struct ld2410_report *last)
{
	int decoded = 0;

	for (size_t i = 0; i < n; i++) {
		struct ld2410_report rep;

		if (ld2410_parser_feed(p, buf[i], &rep)) {
			decoded++;
			if (last != NULL) {
				*last = rep;
			}
		}
	}
	return decoded;
}

ZTEST(ld2410_parser, test_valid_basic_frame_decodes)
{
	struct ld2410_parser p;
	struct ld2410_report rep;
	uint8_t frame[32];

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, basic_payload, 13);

	zassert_equal(feed_all(&p, frame, n, &rep), 1, "one report decodes");
	zassert_equal(rep.target, LD2410_PARSER_TARGET_BOTH);
	zassert_equal(rep.move_distance_cm, 150);
	zassert_equal(rep.move_energy, 80);
	zassert_equal(rep.still_distance_cm, 200);
	zassert_equal(rep.still_energy, 65);
	zassert_equal(rep.detect_distance_cm, 200);
	zassert_equal(p.frames_ok, 1);
	zassert_equal(p.resyncs, 0);
}

ZTEST(ld2410_parser, test_two_frames_back_to_back)
{
	struct ld2410_parser p;
	uint8_t stream[64];

	ld2410_parser_init(&p);
	size_t n = build_frame(stream, basic_payload, 13);
	n += build_frame(stream + n, basic_payload, 13);

	zassert_equal(feed_all(&p, stream, n, NULL), 2);
	zassert_equal(p.frames_ok, 2);
}

ZTEST(ld2410_parser, test_garbage_prefix_is_hunting_not_resync)
{
	/* Noise on the wire before a frame is normal — the parser
	 * hunts for the header. Hunting is not a resync; only an
	 * abandoned sequence is. The counters must not cry wolf.
	 */
	struct ld2410_parser p;
	struct ld2410_report rep;
	uint8_t stream[48];
	const uint8_t noise[] = {0x00, 0x13, 0xF4, 0x99, 0xF4, 0xF3, 0x77};

	ld2410_parser_init(&p);
	memcpy(stream, noise, sizeof(noise));
	size_t n = sizeof(noise) + build_frame(stream + sizeof(noise), basic_payload, 13);

	zassert_equal(feed_all(&p, stream, n, &rep), 1);
	zassert_equal(p.resyncs, 0, "hunting for a header is not a resync");
}

ZTEST(ld2410_parser, test_bad_footer_costs_a_resync_and_recovers)
{
	struct ld2410_parser p;
	struct ld2410_report rep;
	uint8_t frame[32];

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, basic_payload, 13);
	frame[n - 1] = 0xF9; /* corrupt the last footer byte */

	zassert_equal(feed_all(&p, frame, n, &rep), 0, "bad footer: no report");
	zassert_equal(p.resyncs, 1);
	zassert_equal(p.frames_ok, 0);

	n = build_frame(frame, basic_payload, 13);
	zassert_equal(feed_all(&p, frame, n, &rep), 1, "next frame decodes");
	zassert_equal(p.frames_ok, 1);
}

ZTEST(ld2410_parser, test_oversized_length_rejected)
{
	struct ld2410_parser p;
	uint8_t stream[48];
	/* Header + declared length 65: one past the maximum. */
	const uint8_t bad_len[] = {0xF4, 0xF3, 0xF2, 0xF1, 65, 0};

	ld2410_parser_init(&p);
	memcpy(stream, bad_len, sizeof(bad_len));
	size_t n = sizeof(bad_len) + build_frame(stream + sizeof(bad_len), basic_payload, 13);

	zassert_equal(feed_all(&p, stream, n, NULL), 1,
		      "the parser rejects the length, then locks on again");
	zassert_equal(p.resyncs, 1);
}

ZTEST(ld2410_parser, test_undersized_length_rejected)
{
	struct ld2410_parser p;
	const uint8_t bad_len[] = {0xF4, 0xF3, 0xF2, 0xF1, 2, 0};

	ld2410_parser_init(&p);
	zassert_equal(feed_all(&p, bad_len, sizeof(bad_len), NULL), 0);
	zassert_equal(p.resyncs, 1);
}

ZTEST(ld2410_parser, test_bad_payload_marker_rejected)
{
	struct ld2410_parser p;
	uint8_t payload[13];
	uint8_t frame[32];

	memcpy(payload, basic_payload, sizeof(payload));
	payload[1] = 0xAB; /* the 0xAA marker, flipped */

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, payload, 13);

	zassert_equal(feed_all(&p, frame, n, NULL), 0);
	zassert_equal(p.resyncs, 1, "a framed lie still costs a resync");
	zassert_equal(p.frames_ok, 0);
}

ZTEST(ld2410_parser, test_engineering_frame_counted_not_decoded)
{
	struct ld2410_parser p;
	uint8_t payload[13] = {0x01}; /* engineering type, rest zeros */
	uint8_t frame[32];

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, payload, 13);

	zassert_equal(feed_all(&p, frame, n, NULL), 0, "never decoded");
	zassert_equal(p.eng_frames, 1);
	zassert_equal(p.frames_ok, 0);
	zassert_equal(p.resyncs, 0);
}

ZTEST(ld2410_parser, test_max_length_engineering_frame_accepted)
{
	struct ld2410_parser p;
	uint8_t payload[LD2410_MAX_PAYLOAD] = {0x01};
	uint8_t frame[LD2410_MAX_PAYLOAD + 10];

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, payload, LD2410_MAX_PAYLOAD);

	zassert_equal(feed_all(&p, frame, n, NULL), 0);
	zassert_equal(p.eng_frames, 1, "length 64 is the boundary: allowed");
}

ZTEST(ld2410_parser, test_truncated_frame_then_valid_frames)
{
	/* The wire dies mid-frame (a reset, a loose wire). The next
	 * frame's bytes are initially misread as the old frame's
	 * payload — the parser must take exactly one resync and
	 * come back, not stay lost.
	 */
	struct ld2410_parser p;
	uint8_t stream[96];
	uint8_t frame[32];

	ld2410_parser_init(&p);
	build_frame(frame, basic_payload, 13);

	memcpy(stream, frame, 10); /* header + length + 4 payload bytes */
	size_t n = 10;

	n += build_frame(stream + n, basic_payload, 13);
	n += build_frame(stream + n, basic_payload, 13);

	zassert_equal(feed_all(&p, stream, n, NULL), 1, "only the final, intact frame decodes");
	zassert_equal(p.frames_ok, 1);
	zassert_equal(p.resyncs, 1);
}

ZTEST(ld2410_parser, test_boundary_distances)
{
	struct ld2410_parser p;
	struct ld2410_report rep;
	uint8_t payload[13] = {
		0x02, 0xAA, 0x01, 0xFF, 0xFF, /* move distance 65535 */
		0,                            /* move energy 0       */
		0,    0,                      /* still distance 0    */
		100,                          /* still energy 100    */
		0,    0,                      /* detect distance 0   */
		0x55, 0x00,
	};
	uint8_t frame[32];

	ld2410_parser_init(&p);
	size_t n = build_frame(frame, payload, 13);

	zassert_equal(feed_all(&p, frame, n, &rep), 1);
	zassert_equal(rep.move_distance_cm, 65535, "LE assembly, both bytes");
	zassert_equal(rep.detect_distance_cm, 0);
}

ZTEST(ld2410_parser, test_headline_distance_rules)
{
	struct ld2410_report r = {0};

	r.target = LD2410_PARSER_TARGET_STATIONARY;
	r.still_distance_cm = 210;
	r.move_distance_cm = 90;
	zassert_equal(ld2410_report_headline_cm(&r), 210, "a seated person is the occupancy case");

	r.target = LD2410_PARSER_TARGET_BOTH;
	zassert_equal(ld2410_report_headline_cm(&r), 210, "stationary wins");

	r.target = LD2410_PARSER_TARGET_MOVING;
	zassert_equal(ld2410_report_headline_cm(&r), 90);

	r.target = LD2410_PARSER_TARGET_NONE;
	zassert_equal(ld2410_report_headline_cm(&r), 0);

	zassert_equal(ld2410_report_headline_cm(NULL), 0);
}

ZTEST(ld2410_parser, test_instances_are_independent)
{
	/* The whole point of the extraction: no hidden statics.
	 * Two parsers, two byte streams, no cross-talk.
	 */
	struct ld2410_parser a, b;
	uint8_t frame[32];

	ld2410_parser_init(&a);
	ld2410_parser_init(&b);
	size_t n = build_frame(frame, basic_payload, 13);

	zassert_equal(feed_all(&a, frame, n, NULL), 1);
	zassert_equal(a.frames_ok, 1);
	zassert_equal(b.frames_ok, 0);
	zassert_equal(b.resyncs, 0);
}

ZTEST_SUITE(ld2410_parser, NULL, NULL, NULL, NULL, NULL);
