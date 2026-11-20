/* SPDX-License-Identifier: GPL-2.0-only */
/* On-demand GNSS session controller (starts LOC only while clients exist). */
#include "spx-gnss-session.h"

#include <string.h>

void spx_gnss_session_init(struct spx_gnss_session *s, uint8_t session_id,
			   uint32_t interval_ms, bool loc_available,
			   const struct spx_gnss_session_ops *ops, void *ctx)
{
	if (!s)
		return;
	memset(s, 0, sizeof(*s));
	s->session_id = session_id;
	s->interval_ms = interval_ms;
	s->loc_available = loc_available;
	if (ops)
		s->ops = *ops;
	s->ctx = ctx;
}

static int try_start(struct spx_gnss_session *s)
{
	if (!s || s->active || !s->loc_available || s->client_count == 0)
		return 0;
	if (!s->ops.start_loc)
		return -1;
	if (s->ops.start_loc(s->ctx, s->session_id, s->interval_ms) != 0)
		return -1;
	s->active = true;
	return 0;
}

int spx_gnss_session_on_client_connect(struct spx_gnss_session *s)
{
	if (!s)
		return -1;
	s->client_count++;
	return try_start(s);
}

int spx_gnss_session_on_client_disconnect(struct spx_gnss_session *s)
{
	if (!s)
		return -1;
	if (s->client_count == 0)
		return 0;
	s->client_count--;
	if (s->client_count == 0 && s->active) {
		s->active = false;
		if (s->ops.stop_loc)
			return s->ops.stop_loc(s->ctx, s->session_id);
	}
	return 0;
}

void spx_gnss_session_on_loc_lost(struct spx_gnss_session *s)
{
	if (!s)
		return;
	s->loc_available = false;
	s->active = false;
}

int spx_gnss_session_on_loc_ready(struct spx_gnss_session *s)
{
	if (!s)
		return -1;
	s->loc_available = true;
	return try_start(s);
}

int spx_gnss_session_retry_start(struct spx_gnss_session *s)
{
	return try_start(s);
}

int spx_gnss_session_shutdown(struct spx_gnss_session *s)
{
	int rc = 0;

	if (!s)
		return -1;
	if (s->active) {
		s->active = false;
		if (s->ops.stop_loc)
			rc = s->ops.stop_loc(s->ctx, s->session_id);
	}
	s->client_count = 0;
	return rc;
}
