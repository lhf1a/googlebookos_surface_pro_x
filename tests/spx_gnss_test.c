/* SPDX-License-Identifier: GPL-2.0-only */
/* Host unit tests for Surface Pro X QRTR QMI LOC GNSS stack.
 * Build & run with:
 *   gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 *       -Iprobe/gnss tests/spx_gnss_test.c probe/gnss/spx-qmi-loc.c \
 *       probe/gnss/spx-nmea.c probe/gnss/spx-gnss-session.c -lm -o /tmp/spx_gnss_test
 */
#include "spx-gnss-session.h"
#include "spx-nmea.h"
#include "spx-qmi-loc.h"

#include <assert.h>
#include <linux/qrtr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool near_d(double a, double b, double eps)
{
	return fabs(a - b) <= eps;
}

static bool near_f(float a, float b, float eps)
{
	return fabsf(a - b) <= eps;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)(v & 0xFFFFu));
	put16(p + 2, (uint16_t)(v >> 16));
}

static size_t add_tlv(uint8_t *buf, size_t pos, uint8_t type, const void *val,
		      uint16_t len)
{
	buf[pos] = type;
	put16(buf + pos + 1, len);
	if (len > 0)
		memcpy(buf + pos + 3, val, len);
	return pos + 3u + len;
}

static void make_nmea(char *out, size_t cap, const char *body)
{
	uint8_t sum = 0;
	for (const char *p = body; *p; p++)
		sum ^= (uint8_t)*p;
	int n = snprintf(out, cap, "$%s*%02X\r\n", body, (unsigned)sum);
	assert(n > 0 && (size_t)n < cap);
}

static void test_qmi_loc_request_vectors(void)
{
	uint8_t buf[64];
	size_t n;

	/* 1. QMI_LOC_REG_EVENTS_REQ (0x0021): TLV 0x01 uint64 mask = POSITION_REPORT | NMEA (0x05). */
	const uint8_t expect_reg[] = {
		0x00, 0x01, 0x00, 0x21, 0x00, 0x0B, 0x00,
		0x01, 0x08, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	n = spx_qmi_loc_encode_reg_events(
		buf, sizeof(buf), 1u,
		SPX_QMI_LOC_EVENT_POSITION_REPORT | SPX_QMI_LOC_EVENT_NMEA);
	assert(n == sizeof(expect_reg));
	assert(memcmp(buf, expect_reg, sizeof(expect_reg)) == 0);
	assert(spx_qmi_loc_encode_reg_events(buf, sizeof(expect_reg) - 1u, 1u, 4u) == 0);
	assert(spx_qmi_loc_encode_reg_events(buf, sizeof(buf), 0u, 4u) == 0);

	/* 2. QMI_LOC_SET_NMEA_TYPES_REQ (0x003E): TLV 0x01 uint32 mask = GGA|RMC|GSV|GSA|VTG (0x1F). */
	const uint8_t expect_nmea[] = {
		0x00, 0x02, 0x00, 0x3E, 0x00, 0x07, 0x00,
		0x01, 0x04, 0x00, 0x1F, 0x00, 0x00, 0x00,
	};
	n = spx_qmi_loc_encode_set_nmea_types(buf, sizeof(buf), 2u,
					      SPX_QMI_LOC_NMEA_DEFAULT);
	assert(n == sizeof(expect_nmea));
	assert(memcmp(buf, expect_nmea, sizeof(expect_nmea)) == 0);
	assert(spx_qmi_loc_encode_set_nmea_types(buf, sizeof(expect_nmea) - 1u, 2u, 0x1Fu) == 0);

	/* 3. QMI_LOC_START_REQ (0x0022): session_id=1, intermediate=ENABLE(1), interval=1000ms. */
	const uint8_t expect_start[] = {
		0x00, 0x03, 0x00, 0x22, 0x00, 0x12, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x12, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x13, 0x04, 0x00, 0xE8, 0x03, 0x00, 0x00,
	};
	struct spx_qmi_loc_start_params sp = {
		.session_id = 1u,
		.has_fix_recurrence = false,
		.has_intermediate_report_state = true,
		.intermediate_report_state = SPX_QMI_LOC_INTERMEDIATE_ENABLE,
		.has_min_interval_ms = true,
		.min_interval_ms = 1000u,
	};
	n = spx_qmi_loc_encode_start(buf, sizeof(buf), 3u, &sp);
	assert(n == sizeof(expect_start));
	assert(memcmp(buf, expect_start, sizeof(expect_start)) == 0);

	/* Start with optional Fix Recurrence Type TLV 0x10 = PERIODIC (1). */
	sp.has_fix_recurrence = true;
	sp.fix_recurrence = SPX_QMI_LOC_RECURRENCE_PERIODIC;
	n = spx_qmi_loc_encode_start(buf, sizeof(buf), 4u, &sp);
	assert(n == sizeof(expect_start) + 7u);
	assert(buf[11] == 0x10u && buf[12] == 0x04u && buf[14] == 0x01u);

	/* Reject invalid recurrence / intermediate enum values. */
	sp.fix_recurrence = 99u;
	assert(spx_qmi_loc_encode_start(buf, sizeof(buf), 4u, &sp) == 0);
	sp.has_fix_recurrence = false;
	sp.intermediate_report_state = 9u;
	assert(spx_qmi_loc_encode_start(buf, sizeof(buf), 4u, &sp) == 0);

	/* 4. QMI_LOC_STOP_REQ (0x0023): session_id=1. */
	const uint8_t expect_stop[] = {
		0x00, 0x05, 0x00, 0x23, 0x00, 0x04, 0x00,
		0x01, 0x01, 0x00, 0x01,
	};
	n = spx_qmi_loc_encode_stop(buf, sizeof(buf), 5u, 1u);
	assert(n == sizeof(expect_stop));
	assert(memcmp(buf, expect_stop, sizeof(expect_stop)) == 0);
	assert(spx_qmi_loc_encode_stop(buf, sizeof(expect_stop) - 1u, 5u, 1u) == 0);
}

static void test_qmi_loc_responses_and_indications(void)
{
	struct spx_qmi_result res;
	uint8_t ok_resp[] = {
		0x02, 0x01, 0x00, 0x21, 0x00, 0x07, 0x00,
		0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	assert(spx_qmi_loc_decode_response(ok_resp, sizeof(ok_resp), 1u,
					   SPX_QMI_LOC_REG_EVENTS, &res) == 0);
	assert(res.result == 0 && res.error == 0);

	/* QMI error: result=1, error=94 (NOT_SUPPORTED). */
	uint8_t err_resp[] = {
		0x02, 0x02, 0x00, 0x3E, 0x00, 0x07, 0x00,
		0x02, 0x04, 0x00, 0x01, 0x00, 0x5E, 0x00,
	};
	assert(spx_qmi_loc_decode_response(err_resp, sizeof(err_resp), 2u,
					   SPX_QMI_LOC_SET_NMEA_TYPES,
					   &res) == 1);
	assert(res.result == 1u && res.error == 94u);

	/* Malformed response cases: wrong txn, wrong msg, truncated, inconsistent, dup TLV. */
	assert(spx_qmi_loc_decode_response(ok_resp, sizeof(ok_resp), 9u,
					   SPX_QMI_LOC_REG_EVENTS, &res) == -1);
	assert(spx_qmi_loc_decode_response(ok_resp, sizeof(ok_resp), 1u,
					   SPX_QMI_LOC_START, &res) == -1);
	assert(spx_qmi_loc_decode_response(ok_resp, sizeof(ok_resp) - 1u, 1u,
					   SPX_QMI_LOC_REG_EVENTS, &res) == -1);
	err_resp[12] = 0; /* result=1, error=0 -> inconsistent */
	assert(spx_qmi_loc_decode_response(err_resp, sizeof(err_resp), 2u,
					   SPX_QMI_LOC_SET_NMEA_TYPES,
					   &res) == -1);

	uint8_t dup_resp[21];
	memcpy(dup_resp, ok_resp, 14);
	memcpy(dup_resp + 14, ok_resp + 7, 7);
	put16(dup_resp + 5, 14u);
	assert(spx_qmi_loc_decode_response(dup_resp, sizeof(dup_resp), 1u,
					   SPX_QMI_LOC_REG_EVENTS, &res) == -1);

	/* Indication Status (0x003E Set NMEA Types IND). */
	uint8_t status_ind[] = {
		0x04, 0x00, 0x00, 0x3E, 0x00, 0x07, 0x00,
		0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	uint32_t ind_status = 99u;
	assert(spx_qmi_loc_decode_indication_status(
		       status_ind, sizeof(status_ind),
		       SPX_QMI_LOC_SET_NMEA_TYPES, &ind_status) == 0);
	assert(ind_status == SPX_QMI_LOC_IND_STATUS_SUCCESS);
	assert(spx_qmi_loc_decode_indication_status(
		       status_ind, sizeof(status_ind) - 1u,
		       SPX_QMI_LOC_SET_NMEA_TYPES, &ind_status) == -1);

	/* QMI_LOC_EVENT_NMEA_IND (0x0026). */
	const char *raw_nmea = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
	uint8_t nmea_pkt[256];
	size_t nmea_tlv_len = strlen(raw_nmea) + 1u; /* include trailing NUL */
	nmea_pkt[0] = SPX_QMI_FLAG_INDICATION;
	put16(nmea_pkt + 1, 0u);
	put16(nmea_pkt + 3, SPX_QMI_LOC_EVENT_NMEA_IND);
	put16(nmea_pkt + 5, (uint16_t)(3u + nmea_tlv_len));
	size_t end = add_tlv(nmea_pkt, 7u, 0x01u, raw_nmea,
			     (uint16_t)nmea_tlv_len);

	char decoded[128];
	size_t decoded_len = 0;
	assert(spx_qmi_loc_decode_nmea_ind(nmea_pkt, end, decoded,
					   sizeof(decoded), &decoded_len) == 0);
	assert(decoded_len == strlen(raw_nmea) - 2u);
	assert(strcmp(decoded,
		      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47") == 0);

	/* Reject small output buffer, embedded NUL, truncated packet. */
	assert(spx_qmi_loc_decode_nmea_ind(nmea_pkt, end, decoded, 10u,
					   &decoded_len) == -1);
	assert(spx_qmi_loc_decode_nmea_ind(nmea_pkt, end - 1u, decoded,
					   sizeof(decoded), &decoded_len) == -1);
	nmea_pkt[15] = '\0'; /* embedded NUL */
	assert(spx_qmi_loc_decode_nmea_ind(nmea_pkt, end, decoded,
					   sizeof(decoded), &decoded_len) == -1);
}

static void test_qmi_loc_position_report(void)
{
	uint8_t pkt[512];
	size_t pos = 7u;
	uint32_t u32;
	uint64_t u64;
	uint8_t u8;
	double lat = 47.642310;
	double lon = -122.136950;
	float hor_unc = 4.5f;
	float speed_h = 12.25f;
	float speed_u = 0.5f;
	float alt_ell = 123.5f;
	float alt_msl = 142.0f;
	float vert_unc = 6.0f;
	float speed_v = -0.2f;
	float heading = 271.5f;
	float heading_u = 3.0f;
	float dop[3] = {1.8f, 0.9f, 1.5f};
	uint8_t dop_raw[12];
	uint8_t sats_raw[1 + 2 * 3] = {3u, 0x05, 0x00, 0x0D, 0x00, 0x1F, 0x00};
	uint8_t unknown_tlv[3] = {0xAA, 0xBB, 0xCC};

	u32 = SPX_QMI_LOC_SESSION_SUCCESS;
	pos = add_tlv(pkt, pos, 0x01u, &u32, 4u);
	u8 = 1u;
	pos = add_tlv(pkt, pos, 0x02u, &u8, 1u);
	pos = add_tlv(pkt, pos, 0x10u, &lat, 8u);
	pos = add_tlv(pkt, pos, 0x11u, &lon, 8u);
	pos = add_tlv(pkt, pos, 0x12u, &hor_unc, 4u);
	pos = add_tlv(pkt, pos, 0x18u, &speed_h, 4u);
	pos = add_tlv(pkt, pos, 0x19u, &speed_u, 4u);
	pos = add_tlv(pkt, pos, 0x1Au, &alt_ell, 4u);
	pos = add_tlv(pkt, pos, 0x1Bu, &alt_msl, 4u);
	pos = add_tlv(pkt, pos, 0x1Cu, &vert_unc, 4u);
	pos = add_tlv(pkt, pos, 0x1Fu, &speed_v, 4u);
	pos = add_tlv(pkt, pos, 0x20u, &heading, 4u);
	pos = add_tlv(pkt, pos, 0x21u, &heading_u, 4u);
	u32 = 1u; /* SATELLITE */
	pos = add_tlv(pkt, pos, 0x23u, &u32, 4u);
	memcpy(dop_raw, dop, sizeof(dop_raw));
	pos = add_tlv(pkt, pos, 0x24u, dop_raw, sizeof(dop_raw));
	u64 = 1763640000123ULL;
	pos = add_tlv(pkt, pos, 0x25u, &u64, 8u);
	pos = add_tlv(pkt, pos, 0x2Cu, sats_raw, sizeof(sats_raw));
	pos = add_tlv(pkt, pos, 0x55u, unknown_tlv, sizeof(unknown_tlv));

	pkt[0] = SPX_QMI_FLAG_INDICATION;
	put16(pkt + 1, 0u);
	put16(pkt + 3, SPX_QMI_LOC_EVENT_POSITION_REPORT_IND);
	put16(pkt + 5, (uint16_t)(pos - 7u));

	struct spx_qmi_loc_position out;
	assert(spx_qmi_loc_decode_position_ind(pkt, pos, &out) == 0);
	assert(out.session_status == SPX_QMI_LOC_SESSION_SUCCESS);
	assert(out.session_id == 1u);
	assert(out.has_lat && near_d(out.latitude, lat, 1e-9));
	assert(out.has_lon && near_d(out.longitude, lon, 1e-9));
	assert(out.has_hor_unc_circular && near_f(out.hor_unc_circular, hor_unc, 1e-5f));
	assert(out.has_speed_horizontal && near_f(out.speed_horizontal, speed_h, 1e-5f));
	assert(out.has_alt_ellipsoid && near_f(out.alt_ellipsoid, alt_ell, 1e-5f));
	assert(out.has_alt_sealevel && near_f(out.alt_sealevel, alt_msl, 1e-5f));
	assert(out.has_heading && near_f(out.heading, heading, 1e-5f));
	assert(out.has_dop && near_f(out.pdop, 1.8f, 1e-5f) &&
	       near_f(out.hdop, 0.9f, 1e-5f) && near_f(out.vdop, 1.5f, 1e-5f));
	assert(out.has_utc_timestamp_ms && out.utc_timestamp_ms == 1763640000123ULL);
	assert(out.has_sats_used && out.sats_used_count == 3u &&
	       out.sats_used[0] == 5u && out.sats_used[1] == 13u &&
	       out.sats_used[2] == 31u);

	/* Apply decoded QMI Position Report into fix aggregator and format JSON. */
	struct spx_gnss_fix pfix;
	char pjson[512];
	spx_gnss_fix_init(&pfix);
	spx_gnss_fix_apply_position(&pfix, &out);
	assert(pfix.has_position && near_d(pfix.latitude, lat, 1e-9) &&
	       near_d(pfix.longitude, lon, 1e-9));
	assert(pfix.has_altitude && near_d(pfix.altitude_m, alt_msl, 1e-4));
	assert(pfix.has_accuracy && near_d(pfix.accuracy_m, hor_unc, 1e-4));
	assert(pfix.has_speed && near_d(pfix.speed_mps, speed_h, 1e-4));
	assert(pfix.has_bearing && near_d(pfix.bearing_deg, heading, 1e-4));
	assert(pfix.has_utc_ms && pfix.utc_ms == 1763640000123LL);
	assert(pfix.sats_used == 3u);
	assert(spx_gnss_fix_format_json(&pfix, pjson, sizeof(pjson)) > 0);
	assert(strstr(pjson, "\"accuracy_m\":4.50") != NULL);
	assert(strstr(pjson, "\"sats_used\":3") != NULL);

	/* Reject out-of-range latitude (91.0) and NaN. */
	uint8_t bad[64];
	double bad_lat = 91.0;
	size_t bp = 7u;
	u32 = 0;
	bp = add_tlv(bad, bp, 0x01u, &u32, 4u);
	bp = add_tlv(bad, bp, 0x02u, &u8, 1u);
	bp = add_tlv(bad, bp, 0x10u, &bad_lat, 8u);
	bad[0] = SPX_QMI_FLAG_INDICATION;
	put16(bad + 1, 0u);
	put16(bad + 3, SPX_QMI_LOC_EVENT_POSITION_REPORT_IND);
	put16(bad + 5, (uint16_t)(bp - 7u));
	assert(spx_qmi_loc_decode_position_ind(bad, bp, &out) == -1);

	double nan_val = NAN;
	memcpy(bad + 7u + 7u + 4u + 3u, &nan_val, 8u);
	assert(spx_qmi_loc_decode_position_ind(bad, bp, &out) == -1);

	/* Missing mandatory Session ID TLV 0x02. */
	bp = 7u;
	bp = add_tlv(bad, bp, 0x01u, &u32, 4u);
	put16(bad + 5, (uint16_t)(bp - 7u));
	assert(spx_qmi_loc_decode_position_ind(bad, bp, &out) == -1);

	/* Mismatched 0x2C satellite array length. */
	bp = 7u;
	bp = add_tlv(bad, bp, 0x01u, &u32, 4u);
	bp = add_tlv(bad, bp, 0x02u, &u8, 1u);
	uint8_t bad_sats[3] = {2u, 0x01, 0x00}; /* says 2 sats, only 1 provided */
	bp = add_tlv(bad, bp, 0x2Cu, bad_sats, sizeof(bad_sats));
	put16(bad + 5, (uint16_t)(bp - 7u));
	assert(spx_qmi_loc_decode_position_ind(bad, bp, &out) == -1);
}

static void test_qrtr_ctrl_helpers(void)
{
	uint8_t pkt[sizeof(struct qrtr_ctrl_pkt)];
	struct spx_qrtr_service_info info;

	assert(spx_qrtr_encode_new_lookup(pkt, sizeof(pkt),
					  SPX_QMI_SERVICE_LOC) == sizeof(pkt));
	assert(spx_qrtr_decode_ctrl_pkt(pkt, sizeof(pkt), &info) == 0);
	assert(info.cmd == QRTR_TYPE_NEW_LOOKUP &&
	       info.service == SPX_QMI_SERVICE_LOC);

	/* Construct a valid NEW_SERVER for service 0x10 on node 0 port 42. */
	memset(pkt, 0, sizeof(pkt));
	put32(pkt + 0, QRTR_TYPE_NEW_SERVER);
	put32(pkt + 4, SPX_QMI_SERVICE_LOC);
	put32(pkt + 8, 2u);
	put32(pkt + 12, 0u);
	put32(pkt + 16, 42u);
	assert(spx_qrtr_decode_ctrl_pkt(pkt, sizeof(pkt), &info) == 0);
	assert(spx_qrtr_match_new_server(&info, SPX_QMI_SERVICE_LOC));
	assert(!spx_qrtr_match_new_server(&info, 0x11u));

	/* Sentinel (port == 0) and QRTR_PORT_CTRL must never match. */
	info.port = 0;
	assert(!spx_qrtr_match_new_server(&info, SPX_QMI_SERVICE_LOC));
	info.port = QRTR_PORT_CTRL;
	assert(!spx_qrtr_match_new_server(&info, SPX_QMI_SERVICE_LOC));

	/* DEL_SERVER matching. */
	info.cmd = QRTR_TYPE_DEL_SERVER;
	info.node = 0u;
	info.port = 42u;
	assert(spx_qrtr_match_del_server(&info, SPX_QMI_SERVICE_LOC, 0u, 42u));
	assert(!spx_qrtr_match_del_server(&info, SPX_QMI_SERVICE_LOC, 0u, 43u));
}

static void test_nmea_parser_and_aggregator(void)
{
	struct spx_nmea_sentence s;
	struct spx_gnss_fix fix;
	char line[256];

	spx_gnss_fix_init(&fix);

	/* Checksum tests. */
	const char *gga1 = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
	assert(spx_nmea_verify_checksum(gga1, strlen(gga1)));
	const char *gga_lower = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
	assert(spx_nmea_verify_checksum(gga_lower, strlen(gga_lower)));
	const char *gga_bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n";
	assert(!spx_nmea_verify_checksum(gga_bad, strlen(gga_bad)));
	assert(!spx_nmea_verify_checksum("$GPGGA*00junk", 13));
	assert(!spx_nmea_verify_checksum("$GP$GA*00", 9));
	assert(!spx_nmea_verify_checksum("", 0));

	/* Parse $GPGGA. */
	assert(spx_nmea_parse(gga1, strlen(gga1), &s) == 0);
	assert(s.type == SPX_NMEA_GGA && s.talker == SPX_NMEA_TALKER_GP);
	assert(s.u.gga.has_time && s.u.gga.hour == 12u &&
	       s.u.gga.minute == 35u && s.u.gga.second == 19u &&
	       s.u.gga.millis == 0u);
	assert(s.u.gga.has_position &&
	       near_d(s.u.gga.latitude, 48.0 + 7.038 / 60.0, 1e-7) &&
	       near_d(s.u.gga.longitude, 11.0 + 31.0 / 60.0, 1e-7));
	assert(s.u.gga.fix_quality == 1u && s.u.gga.has_sats_used &&
	       s.u.gga.sats_used == 8u);
	assert(s.u.gga.has_hdop && near_d(s.u.gga.hdop, 0.9, 1e-6));
	assert(s.u.gga.has_altitude && near_d(s.u.gga.altitude_m, 545.4, 1e-6));
	assert(s.u.gga.has_geoid_sep && near_d(s.u.gga.geoid_sep_m, 46.9, 1e-6));
	spx_gnss_fix_apply_nmea(&fix, &s);

	/* Parse $GNGGA with South/West and negative altitude + fractional seconds. */
	make_nmea(line, sizeof(line),
		  "GNGGA,235959.250,3351.4080,S,15112.9180,E,2,11,0.75,-12.5,M,22.1,M,,");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.type == SPX_NMEA_GGA && s.talker == SPX_NMEA_TALKER_GN);
	assert(s.u.gga.millis == 250u && s.u.gga.latitude < -33.85 &&
	       s.u.gga.longitude > 151.21 && near_d(s.u.gga.altitude_m, -12.5, 1e-6));

	/* Parse $GNRMC (leap day 2024-02-29) and check speed knots->m/s & UTC epoch ms. */
	make_nmea(line, sizeof(line),
		  "GNRMC,123519.500,A,4738.5386,N,12208.2170,W,10.0,185.25,290224,,,A");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.type == SPX_NMEA_RMC && s.talker == SPX_NMEA_TALKER_GN);
	assert(s.u.rmc.valid && s.u.rmc.has_position);
	assert(near_d(s.u.rmc.latitude, 47.0 + 38.5386 / 60.0, 1e-7));
	assert(near_d(s.u.rmc.longitude, -(122.0 + 8.2170 / 60.0), 1e-7));
	assert(s.u.rmc.has_speed && near_d(s.u.rmc.speed_mps, 5.144444, 1e-4));
	assert(s.u.rmc.has_bearing && near_d(s.u.rmc.bearing_deg, 185.25, 1e-6));
	assert(s.u.rmc.has_date && s.u.rmc.year == 2024u &&
	       s.u.rmc.month == 2u && s.u.rmc.day == 29u);
	/* 2024-02-29T12:35:19.500Z == 1709210119500 ms */
	assert(s.u.rmc.has_utc_ms && s.u.rmc.utc_ms == 1709210119500LL);
	spx_gnss_fix_apply_nmea(&fix, &s);

	/* Invalid non-leap-year Feb 29 (2023-02-29) must be rejected. */
	make_nmea(line, sizeof(line),
		  "GPRMC,123519,A,4738.5386,N,12208.2170,W,0.0,0.0,290223,,,A");
	assert(spx_nmea_parse(line, strlen(line), &s) == -1);

	/* Void RMC without coordinates must parse cleanly as valid=false. */
	make_nmea(line, sizeof(line), "GPRMC,123519,V,,,,,,,230394,,,N");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(!s.u.rmc.valid && !s.u.rmc.has_position);

	/* Parse $GNGSA with NMEA 4.10 system ID. */
	make_nmea(line, sizeof(line),
		  "GNGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1,1");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.type == SPX_NMEA_GSA && s.u.gsa.mode == 'A' &&
	       s.u.gsa.fix_type == 3u && s.u.gsa.sv_count == 5u &&
	       s.u.gsa.sv_ids[0] == 4u && s.u.gsa.sv_ids[4] == 24u &&
	       s.u.gsa.has_pdop && near_d(s.u.gsa.pdop, 2.5, 1e-6) &&
	       s.u.gsa.has_hdop && near_d(s.u.gsa.hdop, 1.3, 1e-6) &&
	       s.u.gsa.has_vdop && near_d(s.u.gsa.vdop, 2.1, 1e-6) &&
	       s.u.gsa.has_system_id && s.u.gsa.system_id == 1u);
	spx_gnss_fix_apply_nmea(&fix, &s);

	/* Multi-constellation GSV: GP, GL, GA, GB. */
	make_nmea(line, sizeof(line),
		  "GPGSV,2,1,05,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.type == SPX_NMEA_GSV && s.u.gsv.constellation == SPX_NMEA_TALKER_GP &&
	       s.u.gsv.sv_count == 4u && s.u.gsv.svs[0].snr_dbhz == 46 &&
	       !s.u.gsv.svs[3].has_snr);
	spx_gnss_fix_apply_nmea(&fix, &s);

	make_nmea(line, sizeof(line), "GPGSV,2,2,05,24,60,120,44");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.u.gsv.sv_count == 1u);
	spx_gnss_fix_apply_nmea(&fix, &s);

	make_nmea(line, sizeof(line), "GLGSV,1,1,02,65,30,210,35,72,55,045,42");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.u.gsv.constellation == SPX_NMEA_TALKER_GL && s.u.gsv.sv_count == 2u);
	spx_gnss_fix_apply_nmea(&fix, &s);

	make_nmea(line, sizeof(line), "GAGSV,1,1,01,11,45,180,38,7");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.u.gsv.constellation == SPX_NMEA_TALKER_GA && s.u.gsv.sv_count == 1u);
	spx_gnss_fix_apply_nmea(&fix, &s);

	make_nmea(line, sizeof(line), "GBGSV,1,1,01,201,50,090,40");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	assert(s.u.gsv.constellation == SPX_NMEA_TALKER_GB && s.u.gsv.sv_count == 1u);
	spx_gnss_fix_apply_nmea(&fix, &s);

	/* 5 GP + 2 GL + 1 GA + 1 GB = 9 satellites in view. */
	assert(fix.sats_in_view == 9u && fix.sv_count == 9u);

	/* New GP msg 1/1 replaces stale GP entries while keeping GL/GA/GB. */
	make_nmea(line, sizeof(line), "GPGSV,1,1,01,05,70,100,48");
	assert(spx_nmea_parse(line, strlen(line), &s) == 0);
	spx_gnss_fix_apply_nmea(&fix, &s);
	assert(fix.sats_in_view == 5u); /* 1 GP + 2 GL + 1 GA + 1 GB */

	/* JSON formatting check. */
	char json[512];
	size_t jlen = spx_gnss_fix_format_json(&fix, json, sizeof(json));
	assert(jlen > 0 && json[jlen - 1u] == '\n');
	assert(strstr(json, "\"fix\":true") != NULL);
	assert(strstr(json, "\"utc_ms\":1709210119500") != NULL);
	assert(strstr(json, "\"sats_in_view\":5") != NULL);
	assert(spx_gnss_fix_format_json(&fix, json, 16u) == 0);
}

static void test_malformed_and_fuzz_inputs(void)
{
	struct spx_nmea_sentence s;
	char line[300];

	/* Adversarial NMEA inputs with valid checksums. */
	const char *bad_bodies[] = {
		"GPGGA,123519,9060.000,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", /* min==60 */
		"GPGGA,123519,9100.000,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", /* lat>90 */
		"GPGGA,123519,4807.038,X,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", /* bad hemi */
		"GPGGA,123519,4807.038,N,18100.000,E,1,08,0.9,545.4,M,46.9,M,,", /* lon>180 */
		"GPGGA,123519,4807.038,N,01131.000,E,1,08,NaN,545.4,M,46.9,M,,", /* NaN */
		"GPGGA,123519,4807.038,N,01131.000,E,1,08,Inf,545.4,M,46.9,M,,", /* Inf */
		"GPGGA,123519,4807.038,N,01131.000,E,1,08,0x1,545.4,M,46.9,M,,", /* hex */
		"GPGGA,256199,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", /* bad time */
		"GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,F,46.9,M,,", /* bad unit */
		"GPRMC,123519,Z,4807.038,N,01131.000,E,1.0,2.0,230394,,,A",      /* bad status */
		"GPRMC,123519,A,4807.038,N,01131.000,E,-1.0,2.0,230394,,,A",     /* neg speed */
		"GPRMC,123519,A,4807.038,N,01131.000,E,1.0,361.0,230394,,,A",    /* bearing>360 */
		"GPGSA,X,3,01,,,,,,,,,,,,1.0,1.0,1.0",                            /* bad mode */
		"GPGSA,A,4,01,,,,,,,,,,,,1.0,1.0,1.0",                            /* bad fix_type */
		"GPGSV,1,2,04,01,40,083,46",                                      /* msg_num>total */
		"GPGSV,1,1,04,01,95,083,46",                                      /* elev>90 */
		"GPGSV,1,1,04,01,40,365,46",                                      /* azim>360 */
		"XXGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", /* bad talker */
		"GPGGA,1,2,3",                                                    /* too few fields */
	};
	for (size_t i = 0; i < sizeof(bad_bodies) / sizeof(bad_bodies[0]); i++) {
		make_nmea(line, sizeof(line), bad_bodies[i]);
		assert(spx_nmea_parse(line, strlen(line), &s) == -1);
	}

	/* Deterministic PRNG fuzzing of both NMEA parser and QMI LOC decoders. */
	uint32_t seed = 0x5A17C0DEu;
	for (int iter = 0; iter < 2000; iter++) {
		uint8_t fuzz[280];
		seed = seed * 1664525u + 1013904223u;
		size_t flen = (size_t)(seed % sizeof(fuzz));
		for (size_t j = 0; j < flen; j++) {
			seed = seed * 1664525u + 1013904223u;
			fuzz[j] = (uint8_t)(seed >> 24);
		}
		(void)spx_nmea_parse((const char *)fuzz, flen, &s);

		struct spx_qmi_result r;
		(void)spx_qmi_loc_decode_response(fuzz, flen, 1u,
						  SPX_QMI_LOC_START, &r);
		struct spx_qmi_loc_position p;
		(void)spx_qmi_loc_decode_position_ind(fuzz, flen, &p);
		char nbuf[260];
		size_t nlen = 0;
		(void)spx_qmi_loc_decode_nmea_ind(fuzz, flen, nbuf,
						  sizeof(nbuf), &nlen);
	}
}

struct mock_loc {
	int starts;
	int stops;
	int fail_start;
	uint8_t last_session;
	uint32_t last_interval;
};

static int mock_start(void *ctx, uint8_t session_id, uint32_t interval_ms)
{
	struct mock_loc *m = (struct mock_loc *)ctx;
	m->last_session = session_id;
	m->last_interval = interval_ms;
	if (m->fail_start)
		return -1;
	m->starts++;
	return 0;
}

static int mock_stop(void *ctx, uint8_t session_id)
{
	struct mock_loc *m = (struct mock_loc *)ctx;
	m->last_session = session_id;
	m->stops++;
	return 0;
}

static void test_on_demand_session_state_machine(void)
{
	struct mock_loc mock;
	struct spx_gnss_session_ops ops = {
		.start_loc = mock_start,
		.stop_loc = mock_stop,
	};
	struct spx_gnss_session sess;

	memset(&mock, 0, sizeof(mock));
	spx_gnss_session_init(&sess, 1u, 1000u, true, &ops, &mock);
	assert(!sess.active && sess.client_count == 0 && mock.starts == 0 &&
	       mock.stops == 0);

	/* First client starts LOC; second/third do not send duplicate START. */
	assert(spx_gnss_session_on_client_connect(&sess) == 0);
	assert(sess.active && sess.client_count == 1 && mock.starts == 1 &&
	       mock.last_session == 1u && mock.last_interval == 1000u);
	assert(spx_gnss_session_on_client_connect(&sess) == 0);
	assert(spx_gnss_session_on_client_connect(&sess) == 0);
	assert(sess.client_count == 3 && mock.starts == 1 && mock.stops == 0);

	/* Disconnecting 2 of 3 keeps LOC active; disconnecting last stops immediately. */
	assert(spx_gnss_session_on_client_disconnect(&sess) == 0);
	assert(spx_gnss_session_on_client_disconnect(&sess) == 0);
	assert(sess.active && sess.client_count == 1 && mock.stops == 0);
	assert(spx_gnss_session_on_client_disconnect(&sess) == 0);
	assert(!sess.active && sess.client_count == 0 && mock.stops == 1);

	/* Underflow-safe extra disconnect does nothing. */
	assert(spx_gnss_session_on_client_disconnect(&sess) == 0);
	assert(sess.client_count == 0 && mock.stops == 1);

	/* Start failure keeps session inactive; retry succeeds when LOC recovers. */
	mock.fail_start = 1;
	assert(spx_gnss_session_on_client_connect(&sess) == -1);
	assert(!sess.active && sess.client_count == 1 && mock.starts == 1);
	mock.fail_start = 0;
	assert(spx_gnss_session_retry_start(&sess) == 0);
	assert(sess.active && mock.starts == 2);

	/* MPSS DEL_SERVER (loc_lost) drops active; loc_ready restarts while client exists. */
	spx_gnss_session_on_loc_lost(&sess);
	assert(!sess.active && !sess.loc_available && sess.client_count == 1);
	assert(spx_gnss_session_on_loc_ready(&sess) == 0);
	assert(sess.active && sess.loc_available && mock.starts == 3);

	/* Clean shutdown sends STOP when active, and does not send STOP when idle. */
	assert(spx_gnss_session_shutdown(&sess) == 0);
	assert(!sess.active && sess.client_count == 0 && mock.stops == 2);
	assert(spx_gnss_session_shutdown(&sess) == 0);
	assert(mock.stops == 2);

	/* Client connects before LOC ready and leaves before LOC ready -> never starts. */
	spx_gnss_session_on_loc_lost(&sess);
	assert(spx_gnss_session_on_client_connect(&sess) == 0);
	assert(!sess.active && mock.starts == 3);
	assert(spx_gnss_session_on_client_disconnect(&sess) == 0);
	assert(spx_gnss_session_on_loc_ready(&sess) == 0);
	assert(!sess.active && mock.starts == 3);
}

static void test_qmi_loc_control_and_sv_extensions(void)
{
	uint8_t buf[128];
	size_t n;

	n = spx_qmi_loc_encode_set_engine_lock(buf, sizeof(buf), 10u,
					       SPX_QMI_LOC_LOCK_NONE);
	assert(n == 14u);
	assert(buf[0] == 0x00u && buf[3] == 0x3Au && buf[4] == 0x00u);
	assert(buf[7] == 0x01u && buf[10] == 0x01u);
	assert(spx_qmi_loc_encode_set_engine_lock(buf, sizeof(buf), 10u, 99u) == 0);

	n = spx_qmi_loc_encode_set_operation_mode(buf, sizeof(buf), 11u,
						  SPX_QMI_LOC_OPMODE_STANDALONE);
	assert(n == 14u);
	assert(buf[3] == 0x4Au && buf[10] == 0x04u);

	n = spx_qmi_loc_encode_inject_utc_time(buf, sizeof(buf), 12u,
					       1700000000000ULL, 2000u, 1);
	assert(n == 32u);
	assert(buf[3] == 0x38u && buf[7] == 0x01u && buf[18] == 0x02u);

	n = spx_qmi_loc_encode_inject_position(buf, sizeof(buf), 13u,
					       37.422, -122.084, 50.0f);
	assert(n == 36u);
	assert(buf[3] == 0x39u && buf[7] == 0x10u);
	assert(spx_qmi_loc_encode_inject_position(buf, sizeof(buf), 13u,
						  91.0, 0.0, 10.0f) == 0);

	n = spx_qmi_loc_encode_delete_all_aiding(buf, sizeof(buf), 14u);
	assert(n == 11u);
	assert(buf[3] == 0x44u && buf[7] == 0x01u && buf[10] == 0x01u);

	/* Live SQ2 MPSS coarse CELLID packet: status=1, tech=CELLID, ellipse unc ~1155km */
	const uint8_t cell_pos[] = {
		0x04, 0x08, 0x00, 0x24, 0x00, 32u, 0x00,
		0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x02, 0x01, 0x00, 0x01,
		0x13, 0x04, 0x00, 0xe4, 0x01, 0x8d, 0x49,
		0x14, 0x04, 0x00, 0xe4, 0x01, 0x8d, 0x49,
		0x23, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00
	};
	struct spx_qmi_loc_position pos;
	assert(spx_qmi_loc_decode_position_ind(cell_pos, sizeof(cell_pos), &pos) == 0);
	assert(pos.session_status == SPX_QMI_LOC_SESSION_IN_PROGRESS);
	assert(!pos.has_hor_unc_circular);
	assert(pos.has_hor_unc_ellipse_minor && pos.has_hor_unc_ellipse_major);
	assert(pos.hor_unc_ellipse_major > 1000000.0f);
	assert(pos.has_technology && pos.technology == SPX_QMI_LOC_TECH_CELLULAR);

	/* Live SQ2 MPSS SV Info indication: altitude_assumed=0, no SVs yet. */
	const uint8_t empty_sv[] = {
		0x04, 0x0c, 0x00, 0x25, 0x00, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00
	};
	struct spx_qmi_loc_sv_info sv_info;
	assert(spx_qmi_loc_decode_sv_info_ind(empty_sv, sizeof(empty_sv),
					      &sv_info) == 0);
	assert(sv_info.has_altitude_assumed && !sv_info.altitude_assumed);
	assert(sv_info.count == 0);

	/* Non-empty SV Info with 1 GPS satellite (28-byte entry). */
	uint8_t one_sv[7u + 4u + 4u + 28u];
	memset(one_sv, 0, sizeof(one_sv));
	one_sv[0] = 0x04;
	one_sv[3] = 0x25;
	one_sv[5] = (uint8_t)(sizeof(one_sv) - 7u);
	one_sv[7] = 0x01; one_sv[8] = 0x01; one_sv[10] = 0x01;
	one_sv[11] = 0x10; one_sv[12] = 29u; one_sv[14] = 1u;
	uint8_t *e = &one_sv[15];
	e[0] = 0xEB;
	e[4] = SPX_QMI_LOC_SYSTEM_GPS;
	e[8] = 12u;
	e[11] = SPX_QMI_LOC_SV_STATUS_TRACKING;
	float elev = 45.0f, az = 120.0f, snr = 34.5f;
	memcpy(e + 16, &elev, 4);
	memcpy(e + 20, &az, 4);
	memcpy(e + 24, &snr, 4);
	assert(spx_qmi_loc_decode_sv_info_ind(one_sv, sizeof(one_sv),
					      &sv_info) == 0);
	assert(sv_info.has_altitude_assumed && sv_info.altitude_assumed);
	assert(sv_info.count == 1u);
	assert(sv_info.svs[0].system == SPX_QMI_LOC_SYSTEM_GPS);
	assert(sv_info.svs[0].sv_id == 12u);
	assert(sv_info.svs[0].snr_dbhz > 34.0f && sv_info.svs[0].snr_dbhz < 35.0f);
}

int main(void)
{
	test_qmi_loc_request_vectors();
	test_qmi_loc_responses_and_indications();
	test_qmi_loc_position_report();
	test_qrtr_ctrl_helpers();
	test_nmea_parser_and_aggregator();
	test_malformed_and_fuzz_inputs();
	test_on_demand_session_state_machine();
	test_qmi_loc_control_and_sv_extensions();
	puts("PASS spx_gnss_test: QMI LOC v2 vectors, QRTR ctrl, NMEA-0183 fuzz, on-demand session");
	return 0;
}
