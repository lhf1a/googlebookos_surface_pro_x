/* SPDX-License-Identifier: GPL-2.0-only */
/* QMI LOC v2 (service 0x10) over QRTR wire encoder/decoder and client.
 * Wire layout reference: freedesktop/libqmi data/qmi-service-loc.json.
 */
#ifndef SPX_QMI_LOC_H
#define SPX_QMI_LOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPX_QMI_SERVICE_LOC 0x0010u

/* QMI SDU header flags (7-byte header over QRTR; no QMUX framing). */
#define SPX_QMI_HEADER_LEN       7u
#define SPX_QMI_FLAG_REQUEST     0x00u
#define SPX_QMI_FLAG_RESPONSE    0x02u
#define SPX_QMI_FLAG_INDICATION  0x04u

/* QMI LOC v2 message IDs. */
#define SPX_QMI_LOC_REG_EVENTS                 0x0021u
#define SPX_QMI_LOC_START                      0x0022u
#define SPX_QMI_LOC_STOP                       0x0023u
#define SPX_QMI_LOC_EVENT_POSITION_REPORT_IND  0x0024u
#define SPX_QMI_LOC_EVENT_GNSS_SV_INFO_IND     0x0025u
#define SPX_QMI_LOC_EVENT_NMEA_IND             0x0026u
#define SPX_QMI_LOC_EVENT_INJECT_TIME_REQ_IND             0x0028u
#define SPX_QMI_LOC_EVENT_INJECT_PREDICTED_ORBITS_REQ_IND 0x0029u
#define SPX_QMI_LOC_EVENT_ENGINE_STATE_IND                0x002Bu
#define SPX_QMI_LOC_INJECT_PREDICTED_ORBITS_DATA          0x0035u
#define SPX_QMI_LOC_GET_PREDICTED_ORBITS_DATA_SOURCE      0x0036u
#define SPX_QMI_LOC_GET_PREDICTED_ORBITS_DATA_VALIDITY    0x0037u
#define SPX_QMI_LOC_INJECT_UTC_TIME                       0x0038u
#define SPX_QMI_LOC_INJECT_POSITION            0x0039u
#define SPX_QMI_LOC_SET_ENGINE_LOCK            0x003Au
#define SPX_QMI_LOC_GET_ENGINE_LOCK            0x003Bu
#define SPX_QMI_LOC_SET_NMEA_TYPES             0x003Eu
#define SPX_QMI_LOC_DELETE_ASSISTANCE_DATA     0x0044u
#define SPX_QMI_LOC_SET_OPERATION_MODE         0x004Au
#define SPX_QMI_LOC_INJECT_XTRA_DATA           0x00A7u

#define SPX_QMI_LOC_MAX_XTRA_PART_LEN          1024u

/* QmiLocEventRegistrationFlag (64-bit mask, TLV 0x01 in 0x0021). */
#define SPX_QMI_LOC_EVENT_POSITION_REPORT   (1ULL << 0)
#define SPX_QMI_LOC_EVENT_GNSS_SV_INFO      (1ULL << 1)
#define SPX_QMI_LOC_EVENT_NMEA              (1ULL << 2)
#define SPX_QMI_LOC_EVENT_INJECT_TIME_REQ             (1ULL << 4)
#define SPX_QMI_LOC_EVENT_INJECT_PREDICTED_ORBITS_REQ (1ULL << 5)
#define SPX_QMI_LOC_EVENT_ENGINE_STATE                (1ULL << 7)
#define SPX_QMI_LOC_EVENT_FIX_SESSION_STATE (1ULL << 8)

/* QmiLocNmeaType (32-bit mask, TLV 0x01 in 0x003E). */
#define SPX_QMI_LOC_NMEA_GGA     (1u << 0)
#define SPX_QMI_LOC_NMEA_RMC     (1u << 1)
#define SPX_QMI_LOC_NMEA_GSV     (1u << 2)
#define SPX_QMI_LOC_NMEA_GSA     (1u << 3)
#define SPX_QMI_LOC_NMEA_VTG     (1u << 4)
#define SPX_QMI_LOC_NMEA_GLGSV   (1u << 7)
#define SPX_QMI_LOC_NMEA_GNGSA   (1u << 8)
#define SPX_QMI_LOC_NMEA_GAGSV   (1u << 11)
#define SPX_QMI_LOC_NMEA_PQGSA   (1u << 15)
#define SPX_QMI_LOC_NMEA_PQGSV   (1u << 16)
#define SPX_QMI_LOC_NMEA_GNGGA   (1u << 19)
#define SPX_QMI_LOC_NMEA_GNRMC   (1u << 20)
#define SPX_QMI_LOC_NMEA_GBGSV   (1u << 25)
#define SPX_QMI_LOC_NMEA_GQGSV   (1u << 28)
#define SPX_QMI_LOC_NMEA_DEFAULT \
	(SPX_QMI_LOC_NMEA_GGA | SPX_QMI_LOC_NMEA_RMC | SPX_QMI_LOC_NMEA_GSV | \
	 SPX_QMI_LOC_NMEA_GSA | SPX_QMI_LOC_NMEA_VTG)
#define SPX_QMI_LOC_NMEA_EXTENDED \
	(SPX_QMI_LOC_NMEA_DEFAULT | SPX_QMI_LOC_NMEA_GLGSV | \
	 SPX_QMI_LOC_NMEA_GNGSA | SPX_QMI_LOC_NMEA_GAGSV | \
	 SPX_QMI_LOC_NMEA_PQGSA | SPX_QMI_LOC_NMEA_PQGSV | \
	 SPX_QMI_LOC_NMEA_GNGGA | SPX_QMI_LOC_NMEA_GNRMC | \
	 SPX_QMI_LOC_NMEA_GBGSV | SPX_QMI_LOC_NMEA_GQGSV)

/* QmiLocLockType (uint32, TLV 0x01 in 0x003A). */
#define SPX_QMI_LOC_LOCK_NONE 1u
#define SPX_QMI_LOC_LOCK_MI   2u
#define SPX_QMI_LOC_LOCK_MT   3u
#define SPX_QMI_LOC_LOCK_ALL  4u

/* QmiLocOperationMode (uint32, TLV 0x01 in 0x004A). */
#define SPX_QMI_LOC_OPMODE_DEFAULT    1u
#define SPX_QMI_LOC_OPMODE_MSB        2u
#define SPX_QMI_LOC_OPMODE_MSA        3u
#define SPX_QMI_LOC_OPMODE_STANDALONE 4u
#define SPX_QMI_LOC_OPMODE_CELLID     5u

/* QmiLocTechnologyUsed bitmask (TLV 0x23 in 0x0024). */
#define SPX_QMI_LOC_TECH_SATELLITE (1u << 0)
#define SPX_QMI_LOC_TECH_CELLULAR  (1u << 1)
#define SPX_QMI_LOC_TECH_WIFI      (1u << 2)
#define SPX_QMI_LOC_TECH_SENSORS   (1u << 3)
#define SPX_QMI_LOC_TECH_REF_LOC   (1u << 4)
#define SPX_QMI_LOC_TECH_INJECTED  (1u << 5)

/* QmiLocSystem (uint32, in 0x0025 SV Info; qmiLocSvSystemEnumT_v02). */
#define SPX_QMI_LOC_SYSTEM_GPS     1u
#define SPX_QMI_LOC_SYSTEM_GALILEO 2u
#define SPX_QMI_LOC_SYSTEM_SBAS    3u
#define SPX_QMI_LOC_SYSTEM_COMPASS 4u
#define SPX_QMI_LOC_SYSTEM_GLONASS 5u
#define SPX_QMI_LOC_SYSTEM_BDS     6u
#define SPX_QMI_LOC_SYSTEM_QZSS    7u
#define SPX_QMI_LOC_SYSTEM_NAVIC   8u

/* QmiLocSatelliteValidInformation bitmask (in 0x0025 SV Info). */
#define SPX_QMI_LOC_SV_VALID_SYSTEM      (1u << 0)
#define SPX_QMI_LOC_SV_VALID_ID          (1u << 1)
#define SPX_QMI_LOC_SV_VALID_HEALTH      (1u << 2)
#define SPX_QMI_LOC_SV_VALID_STATUS      (1u << 3)
#define SPX_QMI_LOC_SV_VALID_INFO_MASK   (1u << 4)
#define SPX_QMI_LOC_SV_VALID_ELEVATION   (1u << 5)
#define SPX_QMI_LOC_SV_VALID_AZIMUTH     (1u << 6)
#define SPX_QMI_LOC_SV_VALID_SNR         (1u << 7)

/* QmiLocSatelliteStatus (in 0x0025 SV Info). */
#define SPX_QMI_LOC_SV_STATUS_IDLE      0u
#define SPX_QMI_LOC_SV_STATUS_SEARCHING 1u
#define SPX_QMI_LOC_SV_STATUS_TRACKING  2u

/* QmiLocFixRecurrenceType (uint32, TLV 0x10 in 0x0022). */
#define SPX_QMI_LOC_RECURRENCE_PERIODIC 1u
#define SPX_QMI_LOC_RECURRENCE_SINGLE   2u

/* QmiLocIntermediateReportState (uint32, TLV 0x12 in 0x0022). */
#define SPX_QMI_LOC_INTERMEDIATE_UNKNOWN 0u
#define SPX_QMI_LOC_INTERMEDIATE_ENABLE  1u
#define SPX_QMI_LOC_INTERMEDIATE_DISABLE 2u

/* QmiLocSessionStatus (uint32, TLV 0x01 in 0x0024). */
#define SPX_QMI_LOC_SESSION_SUCCESS         0u
#define SPX_QMI_LOC_SESSION_IN_PROGRESS     1u
#define SPX_QMI_LOC_SESSION_GENERAL_FAILURE 2u
#define SPX_QMI_LOC_SESSION_TIMEOUT         3u
#define SPX_QMI_LOC_SESSION_USER_ENDED      4u
#define SPX_QMI_LOC_SESSION_BAD_PARAMETER   5u
#define SPX_QMI_LOC_SESSION_ENGINE_LOCKED   6u
#define SPX_QMI_LOC_SESSION_PHONE_OFFLINE   7u

/* QmiLocIndicationStatus (uint32, TLV 0x01 in 0x003E ind). */
#define SPX_QMI_LOC_IND_STATUS_SUCCESS           0u
#define SPX_QMI_LOC_IND_STATUS_GENERAL_FAILURE   1u
#define SPX_QMI_LOC_IND_STATUS_UNSUPPORTED       2u
#define SPX_QMI_LOC_IND_STATUS_INVALID_PARAMETER 3u
#define SPX_QMI_LOC_IND_STATUS_ENGINE_BUSY       4u
#define SPX_QMI_LOC_IND_STATUS_PHONE_OFFLINE     5u
#define SPX_QMI_LOC_IND_STATUS_TIMEOUT           6u

#define SPX_QMI_LOC_MAX_SATS_USED 64u
#define SPX_QMI_LOC_MAX_NMEA_LEN  255u

struct spx_qmi_result {
	uint16_t result;
	uint16_t error;
};

struct spx_qmi_loc_start_params {
	uint8_t session_id;
	bool has_fix_recurrence;
	uint32_t fix_recurrence;
	bool has_intermediate_report_state;
	uint32_t intermediate_report_state;
	bool has_min_interval_ms;
	uint32_t min_interval_ms;
};

struct spx_qmi_loc_position {
	uint32_t session_status;
	uint8_t session_id;

	bool has_lat;
	double latitude;
	bool has_lon;
	double longitude;

	bool has_hor_unc_circular;
	float hor_unc_circular;
	bool has_hor_unc_ellipse_minor;
	float hor_unc_ellipse_minor;
	bool has_hor_unc_ellipse_major;
	float hor_unc_ellipse_major;

	bool has_speed_horizontal;
	float speed_horizontal;
	bool has_speed_unc;
	float speed_unc;

	bool has_alt_ellipsoid;
	float alt_ellipsoid;
	bool has_alt_sealevel;
	float alt_sealevel;
	bool has_vert_unc;
	float vert_unc;
	bool has_speed_vertical;
	float speed_vertical;

	bool has_heading;
	float heading;
	bool has_heading_unc;
	float heading_unc;

	bool has_technology;
	uint32_t technology;

	bool has_dop;
	float pdop;
	float hdop;
	float vdop;

	bool has_utc_timestamp_ms;
	uint64_t utc_timestamp_ms;

	bool has_sats_used;
	uint8_t sats_used_count;
	uint16_t sats_used[SPX_QMI_LOC_MAX_SATS_USED];
};

struct spx_qmi_loc_sv_entry {
	uint32_t valid_mask;
	uint32_t system;
	uint16_t sv_id;
	uint8_t health_status;
	uint32_t satellite_status;
	uint8_t nav_data_mask;
	float elevation_deg;
	float azimuth_deg;
	float snr_dbhz;
};

struct spx_qmi_loc_sv_info {
	bool has_altitude_assumed;
	bool altitude_assumed;
	uint8_t count;
	struct spx_qmi_loc_sv_entry svs[SPX_QMI_LOC_MAX_SATS_USED];
};

struct spx_qrtr_service_info {
	uint32_t cmd;
	uint32_t service;
	uint32_t instance;
	uint32_t node;
	uint32_t port;
};

struct spx_qmi_loc_client {
	int fd;
	uint32_t local_node;
	uint32_t service_node;
	uint32_t service_port;
	uint16_t next_txn;
	bool connected;
};

/* Pure QMI LOC wire encoders (return packet byte length, or 0 on error). */
size_t spx_qmi_loc_encode_reg_events(uint8_t *buf, size_t cap, uint16_t txn,
				     uint64_t event_mask);
size_t spx_qmi_loc_encode_set_nmea_types(uint8_t *buf, size_t cap, uint16_t txn,
					 uint32_t nmea_mask);
size_t spx_qmi_loc_encode_start(uint8_t *buf, size_t cap, uint16_t txn,
				const struct spx_qmi_loc_start_params *params);
size_t spx_qmi_loc_encode_stop(uint8_t *buf, size_t cap, uint16_t txn,
			       uint8_t session_id);
size_t spx_qmi_loc_encode_set_engine_lock(uint8_t *buf, size_t cap,
					  uint16_t txn, uint32_t lock_type);
size_t spx_qmi_loc_encode_set_operation_mode(uint8_t *buf, size_t cap,
					     uint16_t txn, uint32_t mode);
size_t spx_qmi_loc_encode_inject_utc_time(uint8_t *buf, size_t cap,
					  uint16_t txn, uint64_t utc_ms,
					  uint32_t uncertainty_ms,
					  int32_t time_source);
size_t spx_qmi_loc_encode_inject_position(uint8_t *buf, size_t cap,
					  uint16_t txn, double latitude,
					  double longitude,
					  float hor_unc_circular);
size_t spx_qmi_loc_encode_delete_all_aiding(uint8_t *buf, size_t cap,
					    uint16_t txn);
size_t spx_qmi_loc_encode_inject_xtra_part(uint8_t *buf, size_t cap,
					   uint16_t txn, uint16_t msg_id,
					   uint32_t total_size,
					   uint16_t total_parts,
					   uint16_t part_num,
					   const uint8_t *part_data,
					   uint16_t part_len);

/* Pure QMI LOC wire decoders.
 * spx_qmi_loc_decode_response returns:
 *   0  -> valid response, QMI result == 0 and error == 0
 *   1  -> valid response, non-zero QMI error (stored in *out if non-NULL)
 *  -1  -> malformed, truncated, duplicate TLV, or mismatched header
 */
int spx_qmi_loc_decode_response(const uint8_t *buf, size_t len,
				uint16_t expected_txn, uint16_t expected_msg,
				struct spx_qmi_result *out);

/* Decodes TLV 0x01 uint32 indication status (e.g. 0x003E Set NMEA Types IND). */
int spx_qmi_loc_decode_indication_status(const uint8_t *buf, size_t len,
					 uint16_t expected_msg,
					 uint32_t *status_out);

/* Decodes QMI_LOC_EVENT_NMEA_IND (0x0026). Strips trailing CR/LF/NUL.
 * Returns 0 on success, -1 on error.
 */
int spx_qmi_loc_decode_nmea_ind(const uint8_t *buf, size_t len,
				char *out, size_t out_cap, size_t *out_len);

/* Decodes QMI_LOC_EVENT_POSITION_REPORT_IND (0x0024).
 * Returns 0 on success, -1 on malformed/invalid packet.
 */
int spx_qmi_loc_decode_position_ind(const uint8_t *buf, size_t len,
				    struct spx_qmi_loc_position *out);

/* Decodes QMI_LOC_EVENT_GNSS_SV_INFO_IND (0x0025).
 * Returns 0 on success, -1 on malformed/invalid packet.
 */
int spx_qmi_loc_decode_sv_info_ind(const uint8_t *buf, size_t len,
				   struct spx_qmi_loc_sv_info *out);

/* Decodes QMI_LOC_GET_PREDICTED_ORBITS_DATA_VALIDITY_IND (0x0037).
 * Returns 0 on success, -1 on malformed/invalid packet.
 */
int spx_qmi_loc_decode_xtra_validity_ind(const uint8_t *buf, size_t len,
					 uint32_t *status_out,
					 uint64_t *start_utc_sec_out,
					 uint16_t *duration_hours_out);

/* Pure QRTR control packet helpers (host-testable without AF_QIPCRTR). */
size_t spx_qrtr_encode_new_lookup(uint8_t *buf, size_t cap, uint32_t service);
int spx_qrtr_decode_ctrl_pkt(const uint8_t *buf, size_t len,
			     struct spx_qrtr_service_info *out);
bool spx_qrtr_match_new_server(const struct spx_qrtr_service_info *info,
			       uint32_t service);
bool spx_qrtr_match_del_server(const struct spx_qrtr_service_info *info,
			       uint32_t service, uint32_t node, uint32_t port);

/* Live QRTR transport API (AF_QIPCRTR, getsockname before bind, dynamic lookup). */
void spx_qmi_loc_client_init(struct spx_qmi_loc_client *client);
int spx_qmi_loc_open(struct spx_qmi_loc_client *client, int lookup_timeout_ms);
void spx_qmi_loc_close(struct spx_qmi_loc_client *client);

int spx_qmi_loc_set_nmea_types(struct spx_qmi_loc_client *client,
			       uint32_t nmea_mask, int timeout_ms,
			       struct spx_qmi_result *qmi_res);
int spx_qmi_loc_reg_events(struct spx_qmi_loc_client *client,
			   uint64_t event_mask, int timeout_ms,
			   struct spx_qmi_result *qmi_res);
int spx_qmi_loc_start(struct spx_qmi_loc_client *client,
		      const struct spx_qmi_loc_start_params *params,
		      int timeout_ms, struct spx_qmi_result *qmi_res);
int spx_qmi_loc_stop(struct spx_qmi_loc_client *client, uint8_t session_id,
		     int timeout_ms, struct spx_qmi_result *qmi_res);
int spx_qmi_loc_set_engine_lock(struct spx_qmi_loc_client *client,
				uint32_t lock_type, int timeout_ms,
				struct spx_qmi_result *qmi_res);
int spx_qmi_loc_set_operation_mode(struct spx_qmi_loc_client *client,
				   uint32_t mode, int timeout_ms,
				   struct spx_qmi_result *qmi_res);
int spx_qmi_loc_inject_utc_time(struct spx_qmi_loc_client *client,
				uint64_t utc_ms, uint32_t uncertainty_ms,
				int32_t time_source, int timeout_ms,
				struct spx_qmi_result *qmi_res);
int spx_qmi_loc_inject_position(struct spx_qmi_loc_client *client,
				double latitude, double longitude,
				float hor_unc_circular, int timeout_ms,
				struct spx_qmi_result *qmi_res);
int spx_qmi_loc_delete_all_aiding(struct spx_qmi_loc_client *client,
				  int timeout_ms,
				  struct spx_qmi_result *qmi_res);
int spx_qmi_loc_get_xtra_validity(struct spx_qmi_loc_client *client,
				  int timeout_ms, uint64_t *start_utc_sec_out,
				  uint16_t *duration_hours_out,
				  struct spx_qmi_result *qmi_res);
int spx_qmi_loc_inject_xtra_data(struct spx_qmi_loc_client *client,
				 const uint8_t *data, size_t data_len,
				 int timeout_ms,
				 struct spx_qmi_result *qmi_res);

/* Receives one non-blocking/ready datagram on client->fd.
 * Returns:
 *   1 -> QMI packet from LOC service written to buf (*out_len set)
 *   2 -> QRTR control packet from QRTR_PORT_CTRL parsed into *ctrl_out
 *   0 -> ignored packet (unrelated peer or EAGAIN/EINTR)
 *  -1 -> fatal receive error
 */
int spx_qmi_loc_recv(struct spx_qmi_loc_client *client, uint8_t *buf,
		     size_t cap, size_t *out_len,
		     struct spx_qrtr_service_info *ctrl_out);

#ifdef __cplusplus
}
#endif

#endif /* SPX_QMI_LOC_H */
