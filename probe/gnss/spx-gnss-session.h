/* SPDX-License-Identifier: GPL-2.0-only */
/* On-demand GNSS session controller (starts LOC only while clients exist). */
#ifndef SPX_GNSS_SESSION_H
#define SPX_GNSS_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spx_gnss_session_ops {
	int (*start_loc)(void *ctx, uint8_t session_id, uint32_t interval_ms);
	int (*stop_loc)(void *ctx, uint8_t session_id);
};

struct spx_gnss_session {
	uint8_t session_id;
	uint32_t interval_ms;
	size_t client_count;
	bool loc_available;
	bool active;
	struct spx_gnss_session_ops ops;
	void *ctx;
};

void spx_gnss_session_init(struct spx_gnss_session *s, uint8_t session_id,
			   uint32_t interval_ms, bool loc_available,
			   const struct spx_gnss_session_ops *ops, void *ctx);

int spx_gnss_session_on_client_connect(struct spx_gnss_session *s);
int spx_gnss_session_on_client_disconnect(struct spx_gnss_session *s);
void spx_gnss_session_on_loc_lost(struct spx_gnss_session *s);
int spx_gnss_session_on_loc_ready(struct spx_gnss_session *s);
int spx_gnss_session_retry_start(struct spx_gnss_session *s);
int spx_gnss_session_shutdown(struct spx_gnss_session *s);

#ifdef __cplusplus
}
#endif

#endif /* SPX_GNSS_SESSION_H */
