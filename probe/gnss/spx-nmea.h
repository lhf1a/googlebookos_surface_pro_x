/* SPDX-License-Identifier: GPL-2.0-only */
/* Strict, fuzz-safe NMEA-0183 parser and GNSS fix aggregator. */
#ifndef SPX_NMEA_H
#define SPX_NMEA_H

#include "spx-qmi-loc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPX_NMEA_MAX_LEN 255u
#define SPX_NMEA_MAX_SVS 64u

enum spx_nmea_talker {
	SPX_NMEA_TALKER_UNKNOWN = 0,
	SPX_NMEA_TALKER_GP, /* GPS */
	SPX_NMEA_TALKER_GL, /* GLONASS */
	SPX_NMEA_TALKER_GA, /* Galileo */
	SPX_NMEA_TALKER_GB, /* BeiDou (GB/BD) */
	SPX_NMEA_TALKER_GN, /* Multi-GNSS */
	SPX_NMEA_TALKER_GQ, /* QZSS */
};

enum spx_nmea_type {
	SPX_NMEA_UNKNOWN = 0,
	SPX_NMEA_GGA,
	SPX_NMEA_RMC,
	SPX_NMEA_GSA,
	SPX_NMEA_GSV,
};

struct spx_nmea_sv {
	enum spx_nmea_talker constellation;
	uint16_t prn;
	bool has_elevation;
	int16_t elevation_deg;
	bool has_azimuth;
	int16_t azimuth_deg;
	bool has_snr;
	int16_t snr_dbhz;
};

struct spx_nmea_gga {
	bool has_time;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint16_t millis;

	bool has_position;
	double latitude;
	double longitude;

	uint8_t fix_quality;
	bool has_sats_used;
	uint8_t sats_used;
	bool has_hdop;
	double hdop;
	bool has_altitude;
	double altitude_m;
	bool has_geoid_sep;
	double geoid_sep_m;
};

struct spx_nmea_rmc {
	bool has_time;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint16_t millis;

	bool valid;

	bool has_position;
	double latitude;
	double longitude;

	bool has_speed;
	double speed_knots;
	double speed_mps;

	bool has_bearing;
	double bearing_deg;

	bool has_date;
	uint16_t year;
	uint8_t month;
	uint8_t day;

	bool has_utc_ms;
	int64_t utc_ms;
};

struct spx_nmea_gsa {
	char mode;
	uint8_t fix_type;
	uint8_t sv_count;
	uint16_t sv_ids[12];
	bool has_pdop;
	double pdop;
	bool has_hdop;
	double hdop;
	bool has_vdop;
	double vdop;
	bool has_system_id;
	uint8_t system_id;
};

struct spx_nmea_gsv {
	enum spx_nmea_talker constellation;
	uint8_t total_msgs;
	uint8_t msg_num;
	uint8_t sats_in_view;
	uint8_t sv_count;
	struct spx_nmea_sv svs[4];
};

struct spx_nmea_sentence {
	enum spx_nmea_type type;
	enum spx_nmea_talker talker;
	union {
		struct spx_nmea_gga gga;
		struct spx_nmea_rmc rmc;
		struct spx_nmea_gsa gsa;
		struct spx_nmea_gsv gsv;
	} u;
};

struct spx_gnss_fix {
	bool has_position;
	double latitude;
	double longitude;

	bool has_altitude;
	double altitude_m;

	bool has_speed;
	double speed_mps;

	bool has_bearing;
	double bearing_deg;

	bool has_accuracy;
	double accuracy_m;

	bool has_hdop;
	double hdop;
	bool has_pdop;
	double pdop;
	bool has_vdop;
	double vdop;

	uint8_t fix_quality;
	uint8_t fix_type;
	uint8_t sats_used;
	uint8_t sats_in_view;

	bool has_utc_ms;
	int64_t utc_ms;

	uint8_t sv_count;
	struct spx_nmea_sv svs[SPX_NMEA_MAX_SVS];
};

/* Validates NMEA-0183 framing ($...*HH with optional trailing CR/LF) and XOR checksum. */
bool spx_nmea_verify_checksum(const char *line, size_t len);

/* Parses a single NMEA-0183 sentence (GGA/RMC/GSA/GSV).
 * Returns 0 on success, -1 on invalid checksum/syntax/unsupported sentence.
 */
int spx_nmea_parse(const char *line, size_t len, struct spx_nmea_sentence *out);

/* Computes Unix UTC epoch milliseconds from UTC date/time components. */
int64_t spx_nmea_utc_epoch_ms(uint16_t year, uint8_t month, uint8_t day,
			      uint8_t hour, uint8_t minute, uint8_t second,
			      uint16_t millis);

/* Aggregated fix helpers. */
void spx_gnss_fix_init(struct spx_gnss_fix *fix);
void spx_gnss_fix_apply_nmea(struct spx_gnss_fix *fix,
			     const struct spx_nmea_sentence *s);
void spx_gnss_fix_apply_position(struct spx_gnss_fix *fix,
				 const struct spx_qmi_loc_position *pos);

/* Formats current fix state as a single newline-terminated JSON object.
 * Returns written length (excluding NUL), or 0 on buffer overflow/error.
 */
size_t spx_gnss_fix_format_json(const struct spx_gnss_fix *fix,
				char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SPX_NMEA_H */
