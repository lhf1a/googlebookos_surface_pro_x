/* SPDX-License-Identifier: GPL-2.0-only */
/* QMI LOC v2 (service 0x10) over QRTR wire encoder/decoder and client.
 * Wire layout reference: freedesktop/libqmi data/qmi-service-loc.json.
 */
#define _POSIX_C_SOURCE 200809L

#include "spx-qmi-loc.h"

#include <errno.h>
#include <linux/qrtr.h>
#include <math.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_le64(const uint8_t *p)
{
	return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32);
}

static float get_le_f32(const uint8_t *p)
{
	uint32_t bits = get_le32(p);
	float v;
	memcpy(&v, &bits, sizeof(v));
	return v;
}

static double get_le_f64(const uint8_t *p)
{
	uint64_t bits = get_le64(p);
	double v;
	memcpy(&v, &bits, sizeof(v));
	return v;
}

static void put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_le64(uint8_t *p, uint64_t v)
{
	put_le32(p, (uint32_t)(v & 0xFFFFFFFFu));
	put_le32(p + 4, (uint32_t)(v >> 32));
}

static void put_le_f32(uint8_t *p, float v)
{
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	put_le32(p, bits);
}

static void put_le_f64(uint8_t *p, double v)
{
	uint64_t bits;
	memcpy(&bits, &v, sizeof(bits));
	put_le64(p, bits);
}

static int64_t mono_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return -1;
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void write_qmi_header(uint8_t *buf, uint8_t flags, uint16_t txn,
			     uint16_t msg, uint16_t tlv_len)
{
	buf[0] = flags;
	put_le16(buf + 1, txn);
	put_le16(buf + 3, msg);
	put_le16(buf + 5, tlv_len);
}

static size_t append_tlv(uint8_t *buf, size_t cap, size_t pos, uint8_t type,
			 const void *val, uint16_t len)
{
	if (!buf || pos > cap || (size_t)len + 3u > cap - pos)
		return 0;
	buf[pos] = type;
	put_le16(buf + pos + 1, len);
	if (len > 0)
		memcpy(buf + pos + 3, val, len);
	return pos + 3u + len;
}

size_t spx_qmi_loc_encode_reg_events(uint8_t *buf, size_t cap, uint16_t txn,
				     uint64_t event_mask)
{
	uint8_t val[8];
	size_t pos;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 3u + sizeof(val))
		return 0;

	put_le64(val, event_mask);
	pos = append_tlv(buf, cap, SPX_QMI_HEADER_LEN, 0x01u, val,
			 (uint16_t)sizeof(val));
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn, SPX_QMI_LOC_REG_EVENTS,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_set_nmea_types(uint8_t *buf, size_t cap, uint16_t txn,
					 uint32_t nmea_mask)
{
	uint8_t val[4];
	size_t pos;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 3u + sizeof(val))
		return 0;

	put_le32(val, nmea_mask);
	pos = append_tlv(buf, cap, SPX_QMI_HEADER_LEN, 0x01u, val,
			 (uint16_t)sizeof(val));
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn,
			 SPX_QMI_LOC_SET_NMEA_TYPES,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_start(uint8_t *buf, size_t cap, uint16_t txn,
				const struct spx_qmi_loc_start_params *params)
{
	size_t pos = SPX_QMI_HEADER_LEN;
	uint8_t u32_buf[4];

	if (!buf || !params || txn == 0 || cap < SPX_QMI_HEADER_LEN + 4u)
		return 0;

	/* TLV 0x01 (mandatory): Session ID (uint8). */
	pos = append_tlv(buf, cap, pos, 0x01u, &params->session_id, 1u);
	if (pos == 0)
		return 0;

	/* TLV 0x10 (optional): Fix Recurrence Type (uint32). */
	if (params->has_fix_recurrence) {
		if (params->fix_recurrence != SPX_QMI_LOC_RECURRENCE_PERIODIC &&
		    params->fix_recurrence != SPX_QMI_LOC_RECURRENCE_SINGLE)
			return 0;
		put_le32(u32_buf, params->fix_recurrence);
		pos = append_tlv(buf, cap, pos, 0x10u, u32_buf, 4u);
		if (pos == 0)
			return 0;
	}

	/* TLV 0x12 (optional): Intermediate Report State (uint32). */
	if (params->has_intermediate_report_state) {
		if (params->intermediate_report_state >
		    SPX_QMI_LOC_INTERMEDIATE_DISABLE)
			return 0;
		put_le32(u32_buf, params->intermediate_report_state);
		pos = append_tlv(buf, cap, pos, 0x12u, u32_buf, 4u);
		if (pos == 0)
			return 0;
	}

	/* TLV 0x13 (optional): Minimum Interval between Position Reports (uint32 ms). */
	if (params->has_min_interval_ms) {
		put_le32(u32_buf, params->min_interval_ms);
		pos = append_tlv(buf, cap, pos, 0x13u, u32_buf, 4u);
		if (pos == 0)
			return 0;
	}

	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn, SPX_QMI_LOC_START,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_stop(uint8_t *buf, size_t cap, uint16_t txn,
			       uint8_t session_id)
{
	size_t pos;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 4u)
		return 0;

	pos = append_tlv(buf, cap, SPX_QMI_HEADER_LEN, 0x01u, &session_id, 1u);
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn, SPX_QMI_LOC_STOP,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

static size_t encode_u32_tlv1(uint8_t *buf, size_t cap, uint16_t txn,
			      uint16_t msg, uint32_t val)
{
	uint8_t u32_buf[4];
	size_t pos;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 7u)
		return 0;
	put_le32(u32_buf, val);
	pos = append_tlv(buf, cap, SPX_QMI_HEADER_LEN, 0x01u, u32_buf, 4u);
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn, msg,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_set_engine_lock(uint8_t *buf, size_t cap,
					  uint16_t txn, uint32_t lock_type)
{
	if (lock_type < SPX_QMI_LOC_LOCK_NONE ||
	    lock_type > SPX_QMI_LOC_LOCK_ALL)
		return 0;
	return encode_u32_tlv1(buf, cap, txn, SPX_QMI_LOC_SET_ENGINE_LOCK,
			       lock_type);
}

size_t spx_qmi_loc_encode_set_operation_mode(uint8_t *buf, size_t cap,
					     uint16_t txn, uint32_t mode)
{
	if (mode < SPX_QMI_LOC_OPMODE_DEFAULT ||
	    mode > SPX_QMI_LOC_OPMODE_CELLID)
		return 0;
	return encode_u32_tlv1(buf, cap, txn, SPX_QMI_LOC_SET_OPERATION_MODE,
			       mode);
}

size_t spx_qmi_loc_encode_inject_utc_time(uint8_t *buf, size_t cap,
					  uint16_t txn, uint64_t utc_ms,
					  uint32_t uncertainty_ms,
					  int32_t time_source)
{
	uint8_t u64_buf[8];
	uint8_t u32_buf[4];
	size_t pos = SPX_QMI_HEADER_LEN;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 25u)
		return 0;
	put_le64(u64_buf, utc_ms);
	pos = append_tlv(buf, cap, pos, 0x01u, u64_buf, 8u);
	if (pos == 0)
		return 0;
	put_le32(u32_buf, uncertainty_ms);
	pos = append_tlv(buf, cap, pos, 0x02u, u32_buf, 4u);
	if (pos == 0)
		return 0;
	put_le32(u32_buf, (uint32_t)time_source);
	pos = append_tlv(buf, cap, pos, 0x10u, u32_buf, 4u);
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn,
			 SPX_QMI_LOC_INJECT_UTC_TIME,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_inject_position(uint8_t *buf, size_t cap,
					  uint16_t txn, double latitude,
					  double longitude,
					  float hor_unc_circular)
{
	uint8_t f64_buf[8];
	uint8_t f32_buf[4];
	size_t pos = SPX_QMI_HEADER_LEN;

	if (!buf || txn == 0 || !isfinite(latitude) || !isfinite(longitude) ||
	    latitude < -90.0 || latitude > 90.0 ||
	    longitude < -180.0 || longitude > 180.0 ||
	    !isfinite(hor_unc_circular) || hor_unc_circular < 0.0f ||
	    cap < SPX_QMI_HEADER_LEN + 29u)
		return 0;
	put_le_f64(f64_buf, latitude);
	pos = append_tlv(buf, cap, pos, 0x10u, f64_buf, 8u);
	if (pos == 0)
		return 0;
	put_le_f64(f64_buf, longitude);
	pos = append_tlv(buf, cap, pos, 0x11u, f64_buf, 8u);
	if (pos == 0)
		return 0;
	put_le_f32(f32_buf, hor_unc_circular);
	pos = append_tlv(buf, cap, pos, 0x12u, f32_buf, 4u);
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn,
			 SPX_QMI_LOC_INJECT_POSITION,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

size_t spx_qmi_loc_encode_delete_all_aiding(uint8_t *buf, size_t cap,
					    uint16_t txn)
{
	uint8_t del_all = 1u;
	size_t pos;

	if (!buf || txn == 0 || cap < SPX_QMI_HEADER_LEN + 4u)
		return 0;
	pos = append_tlv(buf, cap, SPX_QMI_HEADER_LEN, 0x01u, &del_all, 1u);
	if (pos == 0)
		return 0;
	write_qmi_header(buf, SPX_QMI_FLAG_REQUEST, txn,
			 SPX_QMI_LOC_DELETE_ASSISTANCE_DATA,
			 (uint16_t)(pos - SPX_QMI_HEADER_LEN));
	return pos;
}

int spx_qmi_loc_decode_response(const uint8_t *buf, size_t len,
				uint16_t expected_txn, uint16_t expected_msg,
				struct spx_qmi_result *out)
{
	uint8_t seen[256] = {0};
	bool has_result = false;
	struct spx_qmi_result res = {0, 0};
	size_t pos;

	if (out) {
		out->result = 0;
		out->error = 0;
	}
	if (!buf || len < SPX_QMI_HEADER_LEN)
		return -1;
	if (buf[0] != SPX_QMI_FLAG_RESPONSE ||
	    get_le16(buf + 1) != expected_txn ||
	    get_le16(buf + 3) != expected_msg ||
	    get_le16(buf + 5) != len - SPX_QMI_HEADER_LEN)
		return -1;

	for (pos = SPX_QMI_HEADER_LEN; pos < len;) {
		uint8_t type;
		uint16_t tlen;

		if (len - pos < 3u)
			return -1;
		type = buf[pos];
		tlen = get_le16(buf + pos + 1);
		pos += 3u;
		if (tlen > len - pos || seen[type])
			return -1;
		seen[type] = 1;

		if (type == 0x02u) {
			if (tlen != 4u)
				return -1;
			res.result = get_le16(buf + pos);
			res.error = get_le16(buf + pos + 2);
			has_result = true;
		}
		pos += tlen;
	}

	if (!has_result)
		return -1;
	/* Match probe/lte convention: non-zero result with zero error is invalid. */
	if (res.result != 0 && res.error == 0)
		return -1;
	if (out)
		*out = res;
	return (res.result != 0 || res.error != 0) ? 1 : 0;
}

int spx_qmi_loc_decode_indication_status(const uint8_t *buf, size_t len,
					 uint16_t expected_msg,
					 uint32_t *status_out)
{
	uint8_t seen[256] = {0};
	bool has_status = false;
	uint32_t status = 0;
	size_t pos;

	if (!buf || !status_out || len < SPX_QMI_HEADER_LEN)
		return -1;
	if (buf[0] != SPX_QMI_FLAG_INDICATION ||
	    get_le16(buf + 3) != expected_msg ||
	    get_le16(buf + 5) != len - SPX_QMI_HEADER_LEN)
		return -1;

	for (pos = SPX_QMI_HEADER_LEN; pos < len;) {
		uint8_t type;
		uint16_t tlen;

		if (len - pos < 3u)
			return -1;
		type = buf[pos];
		tlen = get_le16(buf + pos + 1);
		pos += 3u;
		if (tlen > len - pos || seen[type])
			return -1;
		seen[type] = 1;

		if (type == 0x01u) {
			if (tlen != 4u)
				return -1;
			status = get_le32(buf + pos);
			has_status = true;
		}
		pos += tlen;
	}

	if (!has_status)
		return -1;
	*status_out = status;
	return 0;
}

int spx_qmi_loc_decode_nmea_ind(const uint8_t *buf, size_t len,
				char *out, size_t out_cap, size_t *out_len)
{
	uint8_t seen[256] = {0};
	const uint8_t *str = NULL;
	size_t slen = 0;
	size_t pos;

	if (!buf || !out || out_cap < 2u || len < SPX_QMI_HEADER_LEN)
		return -1;
	if (buf[0] != SPX_QMI_FLAG_INDICATION ||
	    get_le16(buf + 3) != SPX_QMI_LOC_EVENT_NMEA_IND ||
	    get_le16(buf + 5) != len - SPX_QMI_HEADER_LEN)
		return -1;

	for (pos = SPX_QMI_HEADER_LEN; pos < len;) {
		uint8_t type;
		uint16_t tlen;

		if (len - pos < 3u)
			return -1;
		type = buf[pos];
		tlen = get_le16(buf + pos + 1);
		pos += 3u;
		if (tlen > len - pos || seen[type])
			return -1;
		seen[type] = 1;

		if (type == 0x01u) {
			str = buf + pos;
			slen = tlen;
		}
		pos += tlen;
	}

	if (!str || slen == 0)
		return -1;

	/* Trim trailing NUL/CR/LF padding bytes allowed by QMI string TLVs. */
	while (slen > 0 && (str[slen - 1] == '\0' || str[slen - 1] == '\r' ||
			    str[slen - 1] == '\n'))
		slen--;

	if (slen == 0 || slen > SPX_QMI_LOC_MAX_NMEA_LEN || slen >= out_cap)
		return -1;

	/* Reject embedded NUL or non-printable control characters. */
	for (size_t i = 0; i < slen; i++) {
		if (str[i] < 0x20u || str[i] > 0x7Eu)
			return -1;
	}

	memcpy(out, str, slen);
	out[slen] = '\0';
	if (out_len)
		*out_len = slen;
	return 0;
}

int spx_qmi_loc_decode_position_ind(const uint8_t *buf, size_t len,
				    struct spx_qmi_loc_position *out)
{
	uint8_t seen[256] = {0};
	bool has_status = false;
	bool has_session_id = false;
	struct spx_qmi_loc_position p;
	size_t pos;

	if (!buf || !out || len < SPX_QMI_HEADER_LEN)
		return -1;
	if (buf[0] != SPX_QMI_FLAG_INDICATION ||
	    get_le16(buf + 3) != SPX_QMI_LOC_EVENT_POSITION_REPORT_IND ||
	    get_le16(buf + 5) != len - SPX_QMI_HEADER_LEN)
		return -1;

	memset(&p, 0, sizeof(p));

	for (pos = SPX_QMI_HEADER_LEN; pos < len;) {
		uint8_t type;
		uint16_t tlen;
		const uint8_t *v;

		if (len - pos < 3u)
			return -1;
		type = buf[pos];
		tlen = get_le16(buf + pos + 1);
		pos += 3u;
		if (tlen > len - pos || seen[type])
			return -1;
		seen[type] = 1;
		v = buf + pos;

		switch (type) {
		case 0x01u: /* Session Status (uint32, mandatory) */
			if (tlen != 4u)
				return -1;
			p.session_status = get_le32(v);
			has_status = true;
			break;
		case 0x02u: /* Session ID (uint8, mandatory) */
			if (tlen != 1u)
				return -1;
			p.session_id = v[0];
			has_session_id = true;
			break;
		case 0x10u: /* Latitude (IEEE-754 double, degrees) */
			if (tlen != 8u)
				return -1;
			p.latitude = get_le_f64(v);
			if (!isfinite(p.latitude) || p.latitude < -90.0 ||
			    p.latitude > 90.0)
				return -1;
			p.has_lat = true;
			break;
		case 0x11u: /* Longitude (IEEE-754 double, degrees) */
			if (tlen != 8u)
				return -1;
			p.longitude = get_le_f64(v);
			if (!isfinite(p.longitude) || p.longitude < -180.0 ||
			    p.longitude > 180.0)
				return -1;
			p.has_lon = true;
			break;
		case 0x12u: /* Horizontal Uncertainty Circular (float, meters) */
			if (tlen != 4u)
				return -1;
			p.hor_unc_circular = get_le_f32(v);
			if (!isfinite(p.hor_unc_circular) ||
			    p.hor_unc_circular < 0.0f)
				return -1;
			p.has_hor_unc_circular = true;
			break;
		case 0x13u: /* Horizontal Uncertainty Elliptical Minor (float, m) */
			if (tlen != 4u)
				return -1;
			p.hor_unc_ellipse_minor = get_le_f32(v);
			if (!isfinite(p.hor_unc_ellipse_minor) ||
			    p.hor_unc_ellipse_minor < 0.0f)
				return -1;
			p.has_hor_unc_ellipse_minor = true;
			break;
		case 0x14u: /* Horizontal Uncertainty Elliptical Major (float, m) */
			if (tlen != 4u)
				return -1;
			p.hor_unc_ellipse_major = get_le_f32(v);
			if (!isfinite(p.hor_unc_ellipse_major) ||
			    p.hor_unc_ellipse_major < 0.0f)
				return -1;
			p.has_hor_unc_ellipse_major = true;
			break;
		case 0x18u: /* Horizontal Speed (float, m/s) */
			if (tlen != 4u)
				return -1;
			p.speed_horizontal = get_le_f32(v);
			if (!isfinite(p.speed_horizontal) ||
			    p.speed_horizontal < 0.0f)
				return -1;
			p.has_speed_horizontal = true;
			break;
		case 0x19u: /* Speed Uncertainty (float, m/s) */
			if (tlen != 4u)
				return -1;
			p.speed_unc = get_le_f32(v);
			if (!isfinite(p.speed_unc) || p.speed_unc < 0.0f)
				return -1;
			p.has_speed_unc = true;
			break;
		case 0x1Au: /* Altitude from Ellipsoid (float, meters) */
			if (tlen != 4u)
				return -1;
			p.alt_ellipsoid = get_le_f32(v);
			if (!isfinite(p.alt_ellipsoid))
				return -1;
			p.has_alt_ellipsoid = true;
			break;
		case 0x1Bu: /* Altitude from Sealevel (float, meters) */
			if (tlen != 4u)
				return -1;
			p.alt_sealevel = get_le_f32(v);
			if (!isfinite(p.alt_sealevel))
				return -1;
			p.has_alt_sealevel = true;
			break;
		case 0x1Cu: /* Vertical Uncertainty (float, meters) */
			if (tlen != 4u)
				return -1;
			p.vert_unc = get_le_f32(v);
			if (!isfinite(p.vert_unc) || p.vert_unc < 0.0f)
				return -1;
			p.has_vert_unc = true;
			break;
		case 0x1Fu: /* Vertical Speed (float, m/s) */
			if (tlen != 4u)
				return -1;
			p.speed_vertical = get_le_f32(v);
			if (!isfinite(p.speed_vertical))
				return -1;
			p.has_speed_vertical = true;
			break;
		case 0x20u: /* Heading (float, degrees [0, 360]) */
			if (tlen != 4u)
				return -1;
			p.heading = get_le_f32(v);
			if (!isfinite(p.heading) || p.heading < 0.0f ||
			    p.heading > 360.0f)
				return -1;
			p.has_heading = true;
			break;
		case 0x21u: /* Heading Uncertainty (float, degrees) */
			if (tlen != 4u)
				return -1;
			p.heading_unc = get_le_f32(v);
			if (!isfinite(p.heading_unc) || p.heading_unc < 0.0f)
				return -1;
			p.has_heading_unc = true;
			break;
		case 0x23u: /* Technology Used (uint32 bitmask) */
			if (tlen != 4u)
				return -1;
			p.technology = get_le32(v);
			p.has_technology = true;
			break;
		case 0x24u: /* DOP (3x float: PDOP, HDOP, VDOP) */
			if (tlen != 12u)
				return -1;
			p.pdop = get_le_f32(v);
			p.hdop = get_le_f32(v + 4);
			p.vdop = get_le_f32(v + 8);
			if (!isfinite(p.pdop) || !isfinite(p.hdop) ||
			    !isfinite(p.vdop) || p.pdop < 0.0f ||
			    p.hdop < 0.0f || p.vdop < 0.0f)
				return -1;
			p.has_dop = true;
			break;
		case 0x25u: /* UTC Timestamp (uint64, ms since Unix epoch) */
			if (tlen != 8u)
				return -1;
			p.utc_timestamp_ms = get_le64(v);
			p.has_utc_timestamp_ms = true;
			break;
		case 0x2Cu: { /* Satellites Used: uint8 count + count * uint16 */
			uint8_t count;
			if (tlen < 1u)
				return -1;
			count = v[0];
			if (tlen != (uint16_t)(1u + 2u * count))
				return -1;
			p.sats_used_count =
				count > SPX_QMI_LOC_MAX_SATS_USED
					? (uint8_t)SPX_QMI_LOC_MAX_SATS_USED
					: count;
			for (uint8_t i = 0; i < p.sats_used_count; i++)
				p.sats_used[i] = get_le16(v + 1u + 2u * i);
			p.has_sats_used = true;
			break;
		}
		default:
			/* Safely ignore unknown optional TLVs. */
			break;
		}
		pos += tlen;
	}

	if (!has_status || !has_session_id)
		return -1;

	*out = p;
	return 0;
}

int spx_qmi_loc_decode_sv_info_ind(const uint8_t *buf, size_t len,
				   struct spx_qmi_loc_sv_info *out)
{
	uint8_t seen[256] = {0};
	struct spx_qmi_loc_sv_info info;
	size_t pos;

	if (!buf || !out || len < SPX_QMI_HEADER_LEN)
		return -1;
	if (buf[0] != SPX_QMI_FLAG_INDICATION ||
	    get_le16(buf + 3) != SPX_QMI_LOC_EVENT_GNSS_SV_INFO_IND ||
	    get_le16(buf + 5) != len - SPX_QMI_HEADER_LEN)
		return -1;

	memset(&info, 0, sizeof(info));

	for (pos = SPX_QMI_HEADER_LEN; pos < len;) {
		uint8_t type;
		uint16_t tlen;
		const uint8_t *v;

		if (len - pos < 3u)
			return -1;
		type = buf[pos];
		tlen = get_le16(buf + pos + 1);
		pos += 3u;
		if (tlen > len - pos || seen[type])
			return -1;
		seen[type] = 1;
		v = buf + pos;

		if (type == 0x01u) {
			if (tlen != 1u)
				return -1;
			info.altitude_assumed = (v[0] != 0);
			info.has_altitude_assumed = true;
		} else if (type == 0x10u) {
			/* uint8 count + count * 28-byte SV struct */
			uint8_t count;
			if (tlen < 1u)
				return -1;
			count = v[0];
			if (tlen != (uint16_t)(1u + 28u * count))
				return -1;
			info.count = count > SPX_QMI_LOC_MAX_SATS_USED
					     ? (uint8_t)SPX_QMI_LOC_MAX_SATS_USED
					     : count;
			for (uint8_t i = 0; i < info.count; i++) {
				const uint8_t *e = v + 1u + 28u * i;
				struct spx_qmi_loc_sv_entry *sv = &info.svs[i];
				sv->valid_mask = get_le32(e + 0);
				sv->system = get_le32(e + 4);
				sv->sv_id = get_le16(e + 8);
				sv->health_status = e[10];
				sv->satellite_status = get_le32(e + 11);
				sv->nav_data_mask = e[15];
				sv->elevation_deg = get_le_f32(e + 16);
				sv->azimuth_deg = get_le_f32(e + 20);
				sv->snr_dbhz = get_le_f32(e + 24);
			}
		}
		pos += tlen;
	}

	*out = info;
	return 0;
}

/* QRTR control packet helpers (little-endian struct qrtr_ctrl_pkt: 20 bytes). */
size_t spx_qrtr_encode_new_lookup(uint8_t *buf, size_t cap, uint32_t service)
{
	if (!buf || cap < sizeof(struct qrtr_ctrl_pkt))
		return 0;
	memset(buf, 0, sizeof(struct qrtr_ctrl_pkt));
	put_le32(buf + 0, QRTR_TYPE_NEW_LOOKUP);
	put_le32(buf + 4, service);
	return sizeof(struct qrtr_ctrl_pkt);
}

int spx_qrtr_decode_ctrl_pkt(const uint8_t *buf, size_t len,
			     struct spx_qrtr_service_info *out)
{
	if (!buf || !out || len < sizeof(struct qrtr_ctrl_pkt))
		return -1;
	out->cmd = get_le32(buf + 0);
	out->service = get_le32(buf + 4);
	out->instance = get_le32(buf + 8);
	out->node = get_le32(buf + 12);
	out->port = get_le32(buf + 16);
	return 0;
}

bool spx_qrtr_match_new_server(const struct spx_qrtr_service_info *info,
			       uint32_t service)
{
	if (!info)
		return false;
	return info->cmd == QRTR_TYPE_NEW_SERVER &&
	       info->service == service &&
	       info->port != 0 && info->port < QRTR_PORT_CTRL;
}

bool spx_qrtr_match_del_server(const struct spx_qrtr_service_info *info,
			       uint32_t service, uint32_t node, uint32_t port)
{
	if (!info)
		return false;
	return info->cmd == QRTR_TYPE_DEL_SERVER &&
	       info->service == service &&
	       info->node == node && info->port == port;
}

void spx_qmi_loc_client_init(struct spx_qmi_loc_client *client)
{
	if (!client)
		return;
	memset(client, 0, sizeof(*client));
	client->fd = -1;
}

void spx_qmi_loc_close(struct spx_qmi_loc_client *client)
{
	if (!client)
		return;
	if (client->fd >= 0) {
		close(client->fd);
		client->fd = -1;
	}
	client->local_node = 0;
	client->service_node = 0;
	client->service_port = 0;
	client->connected = false;
}

int spx_qmi_loc_open(struct spx_qmi_loc_client *client, int lookup_timeout_ms)
{
	struct sockaddr_qrtr local;
	struct sockaddr_qrtr ctrl;
	socklen_t local_len = sizeof(local);
	uint8_t lookup_pkt[sizeof(struct qrtr_ctrl_pkt)];
	size_t lookup_len;
	int64_t deadline;

	if (!client)
		return -1;
	if (lookup_timeout_ms <= 0)
		lookup_timeout_ms = 3000;

	spx_qmi_loc_close(client);

	client->fd = socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (client->fd < 0)
		return -1;

	/* QRTR bind requires the kernel-assigned local node from getsockname(),
	 * never a hardcoded node 0 (see docs/SENSORS.md and probe/sensors/spx-ssc.cpp).
	 */
	memset(&local, 0, sizeof(local));
	local.sq_family = AF_QIPCRTR;
	if (getsockname(client->fd, (struct sockaddr *)&local, &local_len) != 0 ||
	    local_len != sizeof(local) ||
	    bind(client->fd, (struct sockaddr *)&local, local_len) != 0) {
		spx_qmi_loc_close(client);
		return -1;
	}
	client->local_node = local.sq_node;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.sq_family = AF_QIPCRTR;
	ctrl.sq_node = client->local_node;
	ctrl.sq_port = QRTR_PORT_CTRL;

	lookup_len = spx_qrtr_encode_new_lookup(lookup_pkt, sizeof(lookup_pkt),
						SPX_QMI_SERVICE_LOC);
	if (lookup_len == 0 ||
	    sendto(client->fd, lookup_pkt, lookup_len, 0,
		   (const struct sockaddr *)&ctrl,
		   sizeof(ctrl)) != (ssize_t)lookup_len) {
		spx_qmi_loc_close(client);
		return -1;
	}

	deadline = mono_ms();
	if (deadline < 0) {
		spx_qmi_loc_close(client);
		return -1;
	}
	deadline += lookup_timeout_ms;

	for (;;) {
		int64_t now = mono_ms();
		int64_t rem = deadline - now;
		struct pollfd pfd;
		int rc;
		uint8_t rx[256];
		struct sockaddr_qrtr from;
		socklen_t from_len = sizeof(from);
		ssize_t n;
		struct spx_qrtr_service_info info;

		if (now < 0 || rem <= 0)
			break;
		pfd.fd = client->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		rc = poll(&pfd, 1, rem > 60000 ? 60000 : (int)rem);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (rc == 0)
			break;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		memset(&from, 0, sizeof(from));
		n = recvfrom(client->fd, rx, sizeof(rx), MSG_TRUNC,
			     (struct sockaddr *)&from, &from_len);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			break;
		}
		if (from_len != sizeof(from) || from.sq_family != AF_QIPCRTR ||
		    from.sq_port != QRTR_PORT_CTRL ||
		    (size_t)n > sizeof(rx))
			continue;

		if (spx_qrtr_decode_ctrl_pkt(rx, (size_t)n, &info) != 0)
			continue;
		if (spx_qrtr_match_new_server(&info, SPX_QMI_SERVICE_LOC)) {
			client->service_node = info.node;
			client->service_port = info.port;
			client->connected = true;
			return 0;
		}
	}

	spx_qmi_loc_close(client);
	return -1;
}

static uint16_t alloc_txn(struct spx_qmi_loc_client *client)
{
	client->next_txn = (uint16_t)((client->next_txn % 65535u) + 1u);
	return client->next_txn;
}

static int qmi_exchange(struct spx_qmi_loc_client *client, const uint8_t *req,
			size_t req_len, uint16_t txn, uint16_t msg,
			int timeout_ms, struct spx_qmi_result *qmi_res)
{
	struct sockaddr_qrtr dst;
	int64_t deadline;

	if (!client || !client->connected || client->fd < 0 || !req ||
	    req_len < SPX_QMI_HEADER_LEN)
		return -1;
	if (timeout_ms <= 0)
		timeout_ms = 5000;

	memset(&dst, 0, sizeof(dst));
	dst.sq_family = AF_QIPCRTR;
	dst.sq_node = client->service_node;
	dst.sq_port = client->service_port;

	if (sendto(client->fd, req, req_len, 0, (const struct sockaddr *)&dst,
		   sizeof(dst)) != (ssize_t)req_len)
		return -1;

	deadline = mono_ms();
	if (deadline < 0)
		return -1;
	deadline += timeout_ms;

	for (;;) {
		int64_t now = mono_ms();
		int64_t rem = deadline - now;
		struct pollfd pfd;
		int rc;
		uint8_t rx[4096];
		struct sockaddr_qrtr from;
		socklen_t from_len = sizeof(from);
		ssize_t n;

		if (now < 0 || rem <= 0)
			return -1;
		pfd.fd = client->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		rc = poll(&pfd, 1, rem > 60000 ? 60000 : (int)rem);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (rc == 0)
			return -1;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return -1;

		memset(&from, 0, sizeof(from));
		n = recvfrom(client->fd, rx, sizeof(rx), MSG_TRUNC,
			     (struct sockaddr *)&from, &from_len);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		if ((size_t)n > sizeof(rx) || from_len != sizeof(from) ||
		    from.sq_family != AF_QIPCRTR)
			continue;

		if (from.sq_port == QRTR_PORT_CTRL) {
			struct spx_qrtr_service_info info;
			if (spx_qrtr_decode_ctrl_pkt(rx, (size_t)n, &info) == 0 &&
			    spx_qrtr_match_del_server(&info, SPX_QMI_SERVICE_LOC,
						      client->service_node,
						      client->service_port)) {
				client->connected = false;
				return -1;
			}
			continue;
		}

		if (from.sq_node != client->service_node ||
		    from.sq_port != client->service_port)
			continue;
		if ((size_t)n < SPX_QMI_HEADER_LEN)
			continue;
		/* Ignore asynchronous indications while awaiting our response. */
		if (rx[0] != SPX_QMI_FLAG_RESPONSE || get_le16(rx + 1) != txn ||
		    get_le16(rx + 3) != msg)
			continue;

		return spx_qmi_loc_decode_response(rx, (size_t)n, txn, msg,
						   qmi_res);
	}
}

int spx_qmi_loc_set_nmea_types(struct spx_qmi_loc_client *client,
			       uint32_t nmea_mask, int timeout_ms,
			       struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_set_nmea_types(req, sizeof(req), txn,
						nmea_mask);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_SET_NMEA_TYPES,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_reg_events(struct spx_qmi_loc_client *client,
			   uint64_t event_mask, int timeout_ms,
			   struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_reg_events(req, sizeof(req), txn, event_mask);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_REG_EVENTS,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_start(struct spx_qmi_loc_client *client,
		      const struct spx_qmi_loc_start_params *params,
		      int timeout_ms, struct spx_qmi_result *qmi_res)
{
	uint8_t req[64];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected || !params)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_start(req, sizeof(req), txn, params);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_START,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_stop(struct spx_qmi_loc_client *client, uint8_t session_id,
		     int timeout_ms, struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_stop(req, sizeof(req), txn, session_id);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_STOP,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_set_engine_lock(struct spx_qmi_loc_client *client,
				uint32_t lock_type, int timeout_ms,
				struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_set_engine_lock(req, sizeof(req), txn,
						 lock_type);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_SET_ENGINE_LOCK,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_set_operation_mode(struct spx_qmi_loc_client *client,
				   uint32_t mode, int timeout_ms,
				   struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_set_operation_mode(req, sizeof(req), txn,
						    mode);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn,
			    SPX_QMI_LOC_SET_OPERATION_MODE, timeout_ms,
			    qmi_res);
}

int spx_qmi_loc_inject_utc_time(struct spx_qmi_loc_client *client,
				uint64_t utc_ms, uint32_t uncertainty_ms,
				int32_t time_source, int timeout_ms,
				struct spx_qmi_result *qmi_res)
{
	uint8_t req[64];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_inject_utc_time(req, sizeof(req), txn, utc_ms,
						 uncertainty_ms, time_source);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_INJECT_UTC_TIME,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_inject_position(struct spx_qmi_loc_client *client,
				double latitude, double longitude,
				float hor_unc_circular, int timeout_ms,
				struct spx_qmi_result *qmi_res)
{
	uint8_t req[64];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_inject_position(req, sizeof(req), txn,
						 latitude, longitude,
						 hor_unc_circular);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn, SPX_QMI_LOC_INJECT_POSITION,
			    timeout_ms, qmi_res);
}

int spx_qmi_loc_delete_all_aiding(struct spx_qmi_loc_client *client,
				  int timeout_ms,
				  struct spx_qmi_result *qmi_res)
{
	uint8_t req[32];
	uint16_t txn;
	size_t len;

	if (!client || !client->connected)
		return -1;
	txn = alloc_txn(client);
	len = spx_qmi_loc_encode_delete_all_aiding(req, sizeof(req), txn);
	if (len == 0)
		return -1;
	return qmi_exchange(client, req, len, txn,
			    SPX_QMI_LOC_DELETE_ASSISTANCE_DATA, timeout_ms,
			    qmi_res);
}

int spx_qmi_loc_recv(struct spx_qmi_loc_client *client, uint8_t *buf,
		     size_t cap, size_t *out_len,
		     struct spx_qrtr_service_info *ctrl_out)
{
	struct sockaddr_qrtr from;
	socklen_t from_len = sizeof(from);
	ssize_t n;

	if (!client || client->fd < 0 || !buf || cap == 0 || !out_len)
		return -1;
	*out_len = 0;

	memset(&from, 0, sizeof(from));
	n = recvfrom(client->fd, buf, cap, MSG_TRUNC | MSG_DONTWAIT,
		     (struct sockaddr *)&from, &from_len);
	if (n < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	if ((size_t)n > cap || from_len != sizeof(from) ||
	    from.sq_family != AF_QIPCRTR)
		return 0;

	if (from.sq_port == QRTR_PORT_CTRL) {
		if (ctrl_out &&
		    spx_qrtr_decode_ctrl_pkt(buf, (size_t)n, ctrl_out) == 0)
			return 2;
		return 0;
	}

	if (!client->connected || from.sq_node != client->service_node ||
	    from.sq_port != client->service_port)
		return 0;

	*out_len = (size_t)n;
	return 1;
}
