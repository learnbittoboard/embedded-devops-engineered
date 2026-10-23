/*
 * ld2410_parser.c — Course 1's parser state machine, extracted
 * unchanged in behaviour by Chapter 6 of Embedded DevOps,
 * Engineered. The only deliberate differences from the driver
 * version: parser state lives in the caller's struct (no statics),
 * decoded fields leave in a struct instead of a callback, and the
 * two bytes of a little-endian field are assembled by hand so this
 * file needs nothing but the C standard library.
 */

#include "ld2410_parser.h"

#include <string.h>

enum parser_state {
	PS_HUNT,
	PS_LEN,
	PS_PAYLOAD,
	PS_FOOTER
};

static const uint8_t header_seq[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t footer_seq[4] = {0xF8, 0xF7, 0xF6, 0xF5};

static uint16_t get_le16(const uint8_t *b)
{
	return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

void ld2410_parser_init(struct ld2410_parser *p)
{
	memset(p, 0, sizeof(*p));
	p->state = PS_HUNT;
}

static void parser_reset(struct ld2410_parser *p)
{
	p->state = PS_HUNT;
	p->match = 0;
	p->len = 0;
	p->len_bytes = 0;
	p->payload_pos = 0;
}

static void parser_abort(struct ld2410_parser *p)
{
	p->resyncs++;
	parser_reset(p);
}

/*
 * A complete payload sits in p->payload. Validate and decode it.
 * Returns true (and fills *out) for a valid basic report only;
 * engineering frames are counted, invalid payloads cost a resync —
 * the driver's exact accounting.
 */
static bool handle_payload(struct ld2410_parser *p, struct ld2410_report *out)
{
	if (p->payload[0] == LD2410_TYPE_ENGINEERING) {
		p->eng_frames++;
		return false;
	}

	if (p->payload[0] != LD2410_TYPE_BASIC || p->len != 13 || p->payload[1] != 0xAA ||
	    p->payload[11] != 0x55 || p->payload[12] != 0x00) {
		parser_abort(p);
		return false;
	}

	/* The protocol has no checksum, so the markers above are not
	 * enough: the target-state byte can still be anything the wire
	 * (or a fault) invents. Only 0..3 name a target (the enum
	 * below); anything else is a malformed frame. Reject it here,
	 * at the trust boundary, before the value can be published as
	 * a reading and used to index lookup tables by callers.
	 */
	if (p->payload[2] > LD2410_PARSER_TARGET_BOTH) {
		parser_abort(p);
		return false;
	}

	if (out != NULL) {
		out->target = (enum ld2410_parser_target)p->payload[2];
		out->move_distance_cm = get_le16(&p->payload[3]);
		out->move_energy = p->payload[5];
		out->still_distance_cm = get_le16(&p->payload[6]);
		out->still_energy = p->payload[8];
		out->detect_distance_cm = get_le16(&p->payload[9]);
	}
	p->frames_ok++;
	return true;
}

bool ld2410_parser_feed(struct ld2410_parser *p, uint8_t byte, struct ld2410_report *out)
{
	switch (p->state) {
	case PS_HUNT:
		if (byte == header_seq[p->match]) {
			p->match++;
			if (p->match == sizeof(header_seq)) {
				p->state = PS_LEN;
				p->match = 0;
				p->len = 0;
				p->len_bytes = 0;
			}
		} else {
			p->match = (byte == header_seq[0]) ? 1 : 0;
		}
		break;

	case PS_LEN:
		p->len |= (uint16_t)byte << (8 * p->len_bytes);
		p->len_bytes++;
		if (p->len_bytes == 2) {
			if (p->len < 3 || p->len > LD2410_MAX_PAYLOAD) {
				parser_abort(p);
			} else {
				p->state = PS_PAYLOAD;
				p->payload_pos = 0;
			}
		}
		break;

	case PS_PAYLOAD:
		p->payload[p->payload_pos++] = byte;
		if (p->payload_pos >= p->len) {
			p->state = PS_FOOTER;
			p->match = 0;
		}
		break;

	case PS_FOOTER:
		if (byte == footer_seq[p->match]) {
			p->match++;
			if (p->match == sizeof(footer_seq)) {
				bool decoded = handle_payload(p, out);

				parser_reset(p);
				return decoded;
			}
		} else {
			parser_abort(p);
			if (byte == header_seq[0]) {
				p->state = PS_HUNT;
				p->match = 1;
			}
		}
		break;
	}

	return false;
}

uint16_t ld2410_report_headline_cm(const struct ld2410_report *r)
{
	if (r == NULL) {
		return 0;
	}

	switch (r->target) {
	case LD2410_PARSER_TARGET_STATIONARY:
	case LD2410_PARSER_TARGET_BOTH:
		return r->still_distance_cm;
	case LD2410_PARSER_TARGET_MOVING:
		return r->move_distance_cm;
	default:
		return 0;
	}
}
