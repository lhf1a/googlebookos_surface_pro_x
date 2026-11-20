/* SPDX-License-Identifier: GPL-2.0-only */
/* Strict, fuzz-safe NMEA-0183 parser and GNSS fix aggregator. */
#include "spx-nmea.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define KNOTS_TO_MPS 0.5144444444444445

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static size_t trim_crlf(const char *s, size_t len)
{
	if (len > 0 && s[len - 1] == '\n')
		len--;
	if (len > 0 && s[len - 1] == '\r')
		len--;
	return len;
}

bool spx_nmea_verify_checksum(const char *line, size_t len)
{
	size_t trimmed;
	uint8_t calc = 0;
	int hi, lo;

	if (!line || len == 0)
		return false;

	trimmed = trim_crlf(line, len);
	/* Minimum valid framed sentence: "$X*HH" (5 bytes). */
	if (trimmed < 5u || trimmed > SPX_NMEA_MAX_LEN)
		return false;
	if (line[0] != '$' || line[trimmed - 3u] != '*')
		return false;

	hi = hex_nibble(line[trimmed - 2u]);
	lo = hex_nibble(line[trimmed - 1u]);
	if (hi < 0 || lo < 0)
		return false;

	for (size_t i = 1u; i < trimmed - 3u; i++) {
		unsigned char ch = (unsigned char)line[i];
		if (ch < 0x20u || ch > 0x7Eu || ch == '$' || ch == '*')
			return false;
		calc ^= ch;
	}

	return calc == (uint8_t)((hi << 4) | lo);
}

static bool parse_u32(const char *s, size_t len, uint32_t *out)
{
	uint64_t val = 0;

	if (!s || len == 0 || len > 10u || !out)
		return false;
	for (size_t i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		val = val * 10u + (uint64_t)(s[i] - '0');
		if (val > UINT32_MAX)
			return false;
	}
	*out = (uint32_t)val;
	return true;
}

static bool parse_i32(const char *s, size_t len, int32_t *out)
{
	bool neg = false;
	uint32_t mag = 0;

	if (!s || len == 0 || !out)
		return false;
	if (s[0] == '-') {
		neg = true;
		s++;
		len--;
	}
	if (!parse_u32(s, len, &mag))
		return false;
	if (neg) {
		if (mag > 2147483648u)
			return false;
		*out = (mag == 2147483648u) ? INT32_MIN : -(int32_t)mag;
	} else {
		if (mag > (uint32_t)INT32_MAX)
			return false;
		*out = (int32_t)mag;
	}
	return true;
}

static bool parse_double_strict(const char *s, size_t len, bool allow_neg,
				double *out)
{
	bool neg = false;
	bool seen_dot = false;
	bool seen_digit = false;
	double integer = 0.0;
	double frac = 0.0;
	double scale = 1.0;

	if (!s || len == 0 || len > 32u || !out)
		return false;

	if (s[0] == '-') {
		if (!allow_neg)
			return false;
		neg = true;
		s++;
		len--;
	} else if (s[0] == '+') {
		s++;
		len--;
	}
	if (len == 0)
		return false;

	for (size_t i = 0; i < len; i++) {
		char c = s[i];
		if (c == '.') {
			if (seen_dot)
				return false;
			seen_dot = true;
			continue;
		}
		if (c < '0' || c > '9')
			return false;
		seen_digit = true;
		if (!seen_dot) {
			integer = integer * 10.0 + (double)(c - '0');
			if (!isfinite(integer) || integer > 1e12)
				return false;
		} else if (scale < 1e12) {
			scale *= 10.0;
			frac += (double)(c - '0') / scale;
		}
	}

	if (!seen_digit)
		return false;
	*out = neg ? -(integer + frac) : (integer + frac);
	return isfinite(*out);
}

static bool parse_utc_time(const char *s, size_t len, uint8_t *hh, uint8_t *mm,
			   uint8_t *ss, uint16_t *ms)
{
	uint32_t h, m, sec;
	uint16_t millis = 0;

	if (!s || len < 6u)
		return false;
	if (!parse_u32(s, 2u, &h) || !parse_u32(s + 2u, 2u, &m) ||
	    !parse_u32(s + 4u, 2u, &sec))
		return false;
	if (h > 23u || m > 59u || sec > 60u)
		return false;

	if (len > 6u) {
		if (s[6] != '.' || len == 7u || len > 16u)
			return false;
		uint32_t weight = 100u;
		for (size_t i = 7u; i < len; i++) {
			if (s[i] < '0' || s[i] > '9')
				return false;
			if (weight > 0) {
				millis = (uint16_t)(millis +
						    (uint16_t)(s[i] - '0') *
							    weight);
				weight /= 10u;
			}
		}
	}

	*hh = (uint8_t)h;
	*mm = (uint8_t)m;
	*ss = (uint8_t)sec;
	*ms = millis;
	return true;
}

static bool is_leap_year(uint16_t y)
{
	return (y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u);
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
	static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30,
					31, 31, 30, 31, 30, 31};
	if (m < 1u || m > 12u)
		return 0;
	if (m == 2u && is_leap_year(y))
		return 29u;
	return dim[m - 1u];
}

static bool parse_utc_date(const char *s, size_t len, uint16_t *year,
			   uint8_t *month, uint8_t *day)
{
	uint32_t d, m, y2;
	uint16_t full_year;

	if (!s || len != 6u)
		return false;
	if (!parse_u32(s, 2u, &d) || !parse_u32(s + 2u, 2u, &m) ||
	    !parse_u32(s + 4u, 2u, &y2))
		return false;
	full_year = (uint16_t)(y2 >= 80u ? 1900u + y2 : 2000u + y2);
	if (m < 1u || m > 12u || d < 1u ||
	    d > days_in_month(full_year, (uint8_t)m))
		return false;

	*year = full_year;
	*month = (uint8_t)m;
	*day = (uint8_t)d;
	return true;
}

/* Civil date to days since 1970-01-01 (Howard Hinnant algorithm). */
int64_t spx_nmea_utc_epoch_ms(uint16_t year, uint8_t month, uint8_t day,
			      uint8_t hour, uint8_t minute, uint8_t second,
			      uint16_t millis)
{
	int64_t y = (int64_t)year - (month <= 2u ? 1 : 0);
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	uint64_t yoe = (uint64_t)(y - era * 400);
	uint64_t m = month;
	uint64_t doy = (153u * (m + (m > 2u ? (uint64_t)-3 : 9u)) + 2u) / 5u +
		       day - 1u;
	uint64_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	int64_t days = era * 146097 + (int64_t)doe - 719468;
	int64_t sec = days * 86400 + (int64_t)hour * 3600 +
		      (int64_t)minute * 60 + (int64_t)second;
	return sec * 1000 + (int64_t)millis;
}

static bool parse_coord(const char *val, size_t vlen, const char *hemi,
			size_t hlen, size_t deg_digits, double max_deg,
			char pos_h, char neg_h, double *out)
{
	uint32_t deg = 0;
	double minutes = 0.0;
	double res;

	if (!val || !hemi || hlen != 1u || vlen < deg_digits + 2u)
		return false;
	if (hemi[0] != pos_h && hemi[0] != neg_h)
		return false;
	if (!parse_u32(val, deg_digits, &deg))
		return false;
	if (!parse_double_strict(val + deg_digits, vlen - deg_digits, false,
				 &minutes))
		return false;
	if (minutes < 0.0 || minutes >= 60.0)
		return false;
	res = (double)deg + minutes / 60.0;
	if (!isfinite(res) || res > max_deg)
		return false;
	if (hemi[0] == neg_h)
		res = -res;
	*out = res;
	return true;
}

static enum spx_nmea_talker parse_talker(const char *id)
{
	if (id[0] == 'G' && id[1] == 'P')
		return SPX_NMEA_TALKER_GP;
	if (id[0] == 'G' && id[1] == 'L')
		return SPX_NMEA_TALKER_GL;
	if (id[0] == 'G' && id[1] == 'A')
		return SPX_NMEA_TALKER_GA;
	if ((id[0] == 'G' && id[1] == 'B') || (id[0] == 'B' && id[1] == 'D'))
		return SPX_NMEA_TALKER_GB;
	if (id[0] == 'G' && id[1] == 'N')
		return SPX_NMEA_TALKER_GN;
	if (id[0] == 'G' && id[1] == 'Q')
		return SPX_NMEA_TALKER_GQ;
	return SPX_NMEA_TALKER_UNKNOWN;
}

#define MAX_FIELDS 32u

static size_t split_fields(const char *body, size_t body_len,
			   const char *fields[MAX_FIELDS],
			   size_t flens[MAX_FIELDS])
{
	size_t count = 0;
	size_t start = 0;

	for (size_t i = 0; i <= body_len; i++) {
		if (i == body_len || body[i] == ',') {
			if (count >= MAX_FIELDS)
				return 0;
			fields[count] = body + start;
			flens[count] = i - start;
			count++;
			start = i + 1u;
		}
	}
	return count;
}

static int parse_gga(const char *const *f, const size_t *fl, size_t nf,
		     struct spx_nmea_gga *gga)
{
	uint32_t uval;

	if (nf < 15u)
		return -1;
	memset(gga, 0, sizeof(*gga));

	if (fl[1] > 0) {
		if (!parse_utc_time(f[1], fl[1], &gga->hour, &gga->minute,
				    &gga->second, &gga->millis))
			return -1;
		gga->has_time = true;
	}

	if (fl[2] > 0 || fl[3] > 0 || fl[4] > 0 || fl[5] > 0) {
		if (!parse_coord(f[2], fl[2], f[3], fl[3], 2u, 90.0, 'N', 'S',
				 &gga->latitude) ||
		    !parse_coord(f[4], fl[4], f[5], fl[5], 3u, 180.0, 'E', 'W',
				 &gga->longitude))
			return -1;
		gga->has_position = true;
	}

	if (fl[6] > 0) {
		if (!parse_u32(f[6], fl[6], &uval) || uval > 8u)
			return -1;
		gga->fix_quality = (uint8_t)uval;
	}

	if (fl[7] > 0) {
		if (!parse_u32(f[7], fl[7], &uval) || uval > 99u)
			return -1;
		gga->sats_used = (uint8_t)uval;
		gga->has_sats_used = true;
	}

	if (fl[8] > 0) {
		if (!parse_double_strict(f[8], fl[8], false, &gga->hdop))
			return -1;
		gga->has_hdop = true;
	}

	if (fl[9] > 0) {
		if (fl[10] > 0 && !(fl[10] == 1u && f[10][0] == 'M'))
			return -1;
		if (!parse_double_strict(f[9], fl[9], true, &gga->altitude_m))
			return -1;
		gga->has_altitude = true;
	}

	if (fl[11] > 0) {
		if (fl[12] > 0 && !(fl[12] == 1u && f[12][0] == 'M'))
			return -1;
		if (!parse_double_strict(f[11], fl[11], true,
					 &gga->geoid_sep_m))
			return -1;
		gga->has_geoid_sep = true;
	}

	return 0;
}

static int parse_rmc(const char *const *f, const size_t *fl, size_t nf,
		     struct spx_nmea_rmc *rmc)
{
	if (nf < 12u)
		return -1;
	memset(rmc, 0, sizeof(*rmc));

	if (fl[1] > 0) {
		if (!parse_utc_time(f[1], fl[1], &rmc->hour, &rmc->minute,
				    &rmc->second, &rmc->millis))
			return -1;
		rmc->has_time = true;
	}

	if (fl[2] != 1u || (f[2][0] != 'A' && f[2][0] != 'V'))
		return -1;
	rmc->valid = (f[2][0] == 'A');

	if (fl[3] > 0 || fl[4] > 0 || fl[5] > 0 || fl[6] > 0) {
		if (!parse_coord(f[3], fl[3], f[4], fl[4], 2u, 90.0, 'N', 'S',
				 &rmc->latitude) ||
		    !parse_coord(f[5], fl[5], f[6], fl[6], 3u, 180.0, 'E', 'W',
				 &rmc->longitude))
			return -1;
		rmc->has_position = true;
	}

	if (fl[7] > 0) {
		if (!parse_double_strict(f[7], fl[7], false, &rmc->speed_knots))
			return -1;
		rmc->speed_mps = rmc->speed_knots * KNOTS_TO_MPS;
		rmc->has_speed = true;
	}

	if (fl[8] > 0) {
		if (!parse_double_strict(f[8], fl[8], false,
					 &rmc->bearing_deg) ||
		    rmc->bearing_deg > 360.0)
			return -1;
		rmc->has_bearing = true;
	}

	if (fl[9] > 0) {
		if (!parse_utc_date(f[9], fl[9], &rmc->year, &rmc->month,
				    &rmc->day))
			return -1;
		rmc->has_date = true;
	}

	if (rmc->has_date && rmc->has_time) {
		rmc->utc_ms = spx_nmea_utc_epoch_ms(rmc->year, rmc->month,
						    rmc->day, rmc->hour,
						    rmc->minute, rmc->second,
						    rmc->millis);
		rmc->has_utc_ms = true;
	}

	return 0;
}

static int parse_gsa(const char *const *f, const size_t *fl, size_t nf,
		     struct spx_nmea_gsa *gsa)
{
	uint32_t uval;

	if (nf != 18u && nf != 19u)
		return -1;
	memset(gsa, 0, sizeof(*gsa));

	if (fl[1] != 1u || (f[1][0] != 'A' && f[1][0] != 'M'))
		return -1;
	gsa->mode = f[1][0];

	if (!parse_u32(f[2], fl[2], &uval) || uval < 1u || uval > 3u)
		return -1;
	gsa->fix_type = (uint8_t)uval;

	for (size_t i = 0; i < 12u; i++) {
		if (fl[3u + i] == 0)
			continue;
		if (!parse_u32(f[3u + i], fl[3u + i], &uval) || uval == 0 ||
		    uval > 999u)
			return -1;
		gsa->sv_ids[gsa->sv_count++] = (uint16_t)uval;
	}

	if (fl[15] > 0) {
		if (!parse_double_strict(f[15], fl[15], false, &gsa->pdop))
			return -1;
		gsa->has_pdop = true;
	}
	if (fl[16] > 0) {
		if (!parse_double_strict(f[16], fl[16], false, &gsa->hdop))
			return -1;
		gsa->has_hdop = true;
	}
	if (fl[17] > 0) {
		if (!parse_double_strict(f[17], fl[17], false, &gsa->vdop))
			return -1;
		gsa->has_vdop = true;
	}
	if (nf == 19u && fl[18] > 0) {
		if (!parse_u32(f[18], fl[18], &uval) || uval > 15u)
			return -1;
		gsa->system_id = (uint8_t)uval;
		gsa->has_system_id = true;
	}

	return 0;
}

static int parse_gsv(const char *const *f, const size_t *fl, size_t nf,
		     enum spx_nmea_talker talker, struct spx_nmea_gsv *gsv)
{
	uint32_t total, num, in_view;
	size_t sv_fields;
	size_t quads;

	if (nf < 4u)
		return -1;
	sv_fields = nf - 4u;
	/* NMEA 4.10 allows 1 trailing signal ID field after 0..4 SV quads. */
	if ((sv_fields % 4u) > 1u)
		return -1;
	quads = sv_fields / 4u;
	if (quads > 4u)
		return -1;

	memset(gsv, 0, sizeof(*gsv));
	gsv->constellation = talker;

	if (!parse_u32(f[1], fl[1], &total) || total < 1u || total > 32u ||
	    !parse_u32(f[2], fl[2], &num) || num < 1u || num > total ||
	    !parse_u32(f[3], fl[3], &in_view) || in_view > 99u)
		return -1;

	gsv->total_msgs = (uint8_t)total;
	gsv->msg_num = (uint8_t)num;
	gsv->sats_in_view = (uint8_t)in_view;

	for (size_t i = 0; i < quads; i++) {
		size_t base = 4u + i * 4u;
		uint32_t prn, azim, snr;
		int32_t elev;
		struct spx_nmea_sv sv;

		if (fl[base] == 0 && fl[base + 1u] == 0 && fl[base + 2u] == 0 &&
		    fl[base + 3u] == 0)
			continue;

		memset(&sv, 0, sizeof(sv));
		sv.constellation = talker;

		if (!parse_u32(f[base], fl[base], &prn) || prn == 0 ||
		    prn > 999u)
			return -1;
		sv.prn = (uint16_t)prn;

		if (fl[base + 1u] > 0) {
			if (!parse_i32(f[base + 1u], fl[base + 1u], &elev) ||
			    elev < -90 || elev > 90)
				return -1;
			sv.elevation_deg = (int16_t)elev;
			sv.has_elevation = true;
		}
		if (fl[base + 2u] > 0) {
			if (!parse_u32(f[base + 2u], fl[base + 2u], &azim) ||
			    azim > 360u)
				return -1;
			sv.azimuth_deg = (int16_t)azim;
			sv.has_azimuth = true;
		}
		if (fl[base + 3u] > 0) {
			if (!parse_u32(f[base + 3u], fl[base + 3u], &snr) ||
			    snr > 99u)
				return -1;
			sv.snr_dbhz = (int16_t)snr;
			sv.has_snr = true;
		}

		gsv->svs[gsv->sv_count++] = sv;
	}

	return 0;
}

int spx_nmea_parse(const char *line, size_t len, struct spx_nmea_sentence *out)
{
	size_t trimmed;
	const char *body;
	size_t body_len;
	const char *fields[MAX_FIELDS];
	size_t flens[MAX_FIELDS];
	size_t nf;
	enum spx_nmea_talker talker;

	if (!line || !out)
		return -1;
	if (!spx_nmea_verify_checksum(line, len))
		return -1;

	trimmed = trim_crlf(line, len);
	body = line + 1u;
	body_len = trimmed - 4u; /* exclude leading '$' and trailing '*HH' */
	nf = split_fields(body, body_len, fields, flens);
	if (nf == 0 || flens[0] != 5u)
		return -1;

	talker = parse_talker(fields[0]);
	if (talker == SPX_NMEA_TALKER_UNKNOWN)
		return -1;

	memset(out, 0, sizeof(*out));
	out->talker = talker;

	if (memcmp(fields[0] + 2u, "GGA", 3u) == 0) {
		if (parse_gga(fields, flens, nf, &out->u.gga) != 0)
			return -1;
		out->type = SPX_NMEA_GGA;
		return 0;
	}
	if (memcmp(fields[0] + 2u, "RMC", 3u) == 0) {
		if (parse_rmc(fields, flens, nf, &out->u.rmc) != 0)
			return -1;
		out->type = SPX_NMEA_RMC;
		return 0;
	}
	if (memcmp(fields[0] + 2u, "GSA", 3u) == 0) {
		if (parse_gsa(fields, flens, nf, &out->u.gsa) != 0)
			return -1;
		out->type = SPX_NMEA_GSA;
		return 0;
	}
	if (memcmp(fields[0] + 2u, "GSV", 3u) == 0) {
		if (parse_gsv(fields, flens, nf, talker, &out->u.gsv) != 0)
			return -1;
		out->type = SPX_NMEA_GSV;
		return 0;
	}

	return -1;
}

void spx_gnss_fix_init(struct spx_gnss_fix *fix)
{
	if (fix)
		memset(fix, 0, sizeof(*fix));
}

static void upsert_sv(struct spx_gnss_fix *fix, const struct spx_nmea_sv *sv)
{
	for (uint8_t i = 0; i < fix->sv_count; i++) {
		if (fix->svs[i].constellation == sv->constellation &&
		    fix->svs[i].prn == sv->prn) {
			fix->svs[i] = *sv;
			return;
		}
	}
	if (fix->sv_count < SPX_NMEA_MAX_SVS)
		fix->svs[fix->sv_count++] = *sv;
}

static void clear_constellation_svs(struct spx_gnss_fix *fix,
				    enum spx_nmea_talker constellation)
{
	uint8_t write = 0;
	for (uint8_t i = 0; i < fix->sv_count; i++) {
		if (fix->svs[i].constellation != constellation)
			fix->svs[write++] = fix->svs[i];
	}
	fix->sv_count = write;
}

void spx_gnss_fix_apply_nmea(struct spx_gnss_fix *fix,
			     const struct spx_nmea_sentence *s)
{
	if (!fix || !s)
		return;

	switch (s->type) {
	case SPX_NMEA_GGA: {
		const struct spx_nmea_gga *g = &s->u.gga;
		fix->fix_quality = g->fix_quality;
		if (g->fix_quality > 0 && g->has_position) {
			fix->latitude = g->latitude;
			fix->longitude = g->longitude;
			fix->has_position = true;
		}
		if (g->has_sats_used)
			fix->sats_used = g->sats_used;
		if (g->has_hdop) {
			fix->hdop = g->hdop;
			fix->has_hdop = true;
		}
		if (g->has_altitude) {
			fix->altitude_m = g->altitude_m;
			fix->has_altitude = true;
		}
		break;
	}
	case SPX_NMEA_RMC: {
		const struct spx_nmea_rmc *r = &s->u.rmc;
		if (r->valid && r->has_position) {
			fix->latitude = r->latitude;
			fix->longitude = r->longitude;
			fix->has_position = true;
		}
		if (r->valid && r->has_speed) {
			fix->speed_mps = r->speed_mps;
			fix->has_speed = true;
		}
		if (r->valid && r->has_bearing) {
			fix->bearing_deg = r->bearing_deg;
			fix->has_bearing = true;
		}
		if (r->has_utc_ms) {
			fix->utc_ms = r->utc_ms;
			fix->has_utc_ms = true;
		}
		break;
	}
	case SPX_NMEA_GSA: {
		const struct spx_nmea_gsa *g = &s->u.gsa;
		fix->fix_type = g->fix_type;
		if (g->has_pdop) {
			fix->pdop = g->pdop;
			fix->has_pdop = true;
		}
		if (g->has_hdop) {
			fix->hdop = g->hdop;
			fix->has_hdop = true;
		}
		if (g->has_vdop) {
			fix->vdop = g->vdop;
			fix->has_vdop = true;
		}
		break;
	}
	case SPX_NMEA_GSV: {
		const struct spx_nmea_gsv *g = &s->u.gsv;
		if (g->msg_num == 1u)
			clear_constellation_svs(fix, g->constellation);
		for (uint8_t i = 0; i < g->sv_count; i++)
			upsert_sv(fix, &g->svs[i]);
		fix->sats_in_view = fix->sv_count;
		break;
	}
	default:
		break;
	}
}

void spx_gnss_fix_apply_position(struct spx_gnss_fix *fix,
				 const struct spx_qmi_loc_position *pos)
{
	if (!fix || !pos)
		return;
	if (pos->session_status != SPX_QMI_LOC_SESSION_SUCCESS &&
	    pos->session_status != SPX_QMI_LOC_SESSION_IN_PROGRESS)
		return;

	if (pos->has_lat && pos->has_lon) {
		fix->latitude = pos->latitude;
		fix->longitude = pos->longitude;
		fix->has_position = true;
		if (fix->fix_quality == 0)
			fix->fix_quality = 1u;
	}
	if (pos->has_alt_sealevel) {
		fix->altitude_m = (double)pos->alt_sealevel;
		fix->has_altitude = true;
	} else if (pos->has_alt_ellipsoid) {
		fix->altitude_m = (double)pos->alt_ellipsoid;
		fix->has_altitude = true;
	}
	if (pos->has_speed_horizontal) {
		fix->speed_mps = (double)pos->speed_horizontal;
		fix->has_speed = true;
	}
	if (pos->has_heading) {
		fix->bearing_deg = (double)pos->heading;
		fix->has_bearing = true;
	}
	if (pos->has_hor_unc_circular) {
		fix->accuracy_m = (double)pos->hor_unc_circular;
		fix->has_accuracy = true;
	}
	if (pos->has_dop) {
		fix->pdop = (double)pos->pdop;
		fix->hdop = (double)pos->hdop;
		fix->vdop = (double)pos->vdop;
		fix->has_pdop = true;
		fix->has_hdop = true;
		fix->has_vdop = true;
	}
	if (pos->has_utc_timestamp_ms && pos->utc_timestamp_ms <= (uint64_t)INT64_MAX) {
		fix->utc_ms = (int64_t)pos->utc_timestamp_ms;
		fix->has_utc_ms = true;
	}
	if (pos->has_sats_used)
		fix->sats_used = pos->sats_used_count;
}

static size_t append_fmt(char *buf, size_t cap, size_t pos, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

#include <stdarg.h>

static size_t append_fmt(char *buf, size_t cap, size_t pos, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (pos == 0 && fmt[0] != '{')
		return 0;
	if (pos >= cap)
		return 0;
	va_start(ap, fmt);
	n = vsnprintf(buf + pos, cap - pos, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= cap - pos)
		return 0;
	return pos + (size_t)n;
}

size_t spx_gnss_fix_format_json(const struct spx_gnss_fix *fix,
				char *buf, size_t cap)
{
	size_t pos = 0;

	if (!fix || !buf || cap < 32u)
		return 0;

	pos = append_fmt(buf, cap, pos, "{\"fix\":%s",
			 fix->has_position ? "true" : "false");
	if (pos == 0)
		return 0;

	if (fix->has_position) {
		pos = append_fmt(buf, cap, pos, ",\"lat\":%.7f,\"lon\":%.7f",
				 fix->latitude, fix->longitude);
		if (pos == 0)
			return 0;
	}
	if (fix->has_altitude) {
		pos = append_fmt(buf, cap, pos, ",\"alt_m\":%.2f",
				 fix->altitude_m);
		if (pos == 0)
			return 0;
	}
	if (fix->has_speed) {
		pos = append_fmt(buf, cap, pos, ",\"speed_mps\":%.3f",
				 fix->speed_mps);
		if (pos == 0)
			return 0;
	}
	if (fix->has_bearing) {
		pos = append_fmt(buf, cap, pos, ",\"bearing_deg\":%.2f",
				 fix->bearing_deg);
		if (pos == 0)
			return 0;
	}
	if (fix->has_accuracy) {
		pos = append_fmt(buf, cap, pos, ",\"accuracy_m\":%.2f",
				 fix->accuracy_m);
		if (pos == 0)
			return 0;
	}
	if (fix->has_hdop) {
		pos = append_fmt(buf, cap, pos, ",\"hdop\":%.2f", fix->hdop);
		if (pos == 0)
			return 0;
	}
	if (fix->has_utc_ms) {
		pos = append_fmt(buf, cap, pos, ",\"utc_ms\":%" PRId64,
				 fix->utc_ms);
		if (pos == 0)
			return 0;
	}

	pos = append_fmt(buf, cap, pos,
			 ",\"sats_used\":%u,\"sats_in_view\":%u}\n",
			 (unsigned)fix->sats_used, (unsigned)fix->sats_in_view);
	return pos;
}
