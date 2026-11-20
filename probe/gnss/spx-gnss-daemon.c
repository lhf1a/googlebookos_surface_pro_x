/* SPDX-License-Identifier: GPL-2.0-only */
/* On-demand QRTR QMI LOC GNSS daemon for Surface Pro X (SC8180X MPSS).
 *
 * Bridges QMI_SERVICE_LOC (0x10) over AF_QIPCRTR to a Unix stream socket.
 * Starts the modem GNSS session only while >= 1 client is connected and
 * sends QMI_LOC_STOP_REQ immediately when the last client disconnects.
 * Sleeps indefinitely in poll(-1) when idle (zero background polling).
 */
#define _POSIX_C_SOURCE 200809L

#include "spx-gnss-session.h"
#include "spx-nmea.h"
#include "spx-qmi-loc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define SPX_GNSS_DEFAULT_SOCK       "/run/spx-gnss.sock"
#define SPX_GNSS_DEFAULT_INTERVAL   1000u
#define SPX_GNSS_DEFAULT_SESSION_ID 1u
#define SPX_GNSS_DEFAULT_SOCK_MODE  0660u
#define SPX_GNSS_MAX_CLIENTS        32u
#define SPX_GNSS_QMI_TIMEOUT_MS     3000
#define SPX_GNSS_RETRY_MS           2000

enum output_mode {
	MODE_NMEA = 0,
	MODE_JSON,
	MODE_BOTH,
};

struct daemon_state {
	const char *sock_path;
	mode_t sock_mode;
	enum output_mode mode;
	uint8_t session_id;
	uint32_t interval_ms;
	uint32_t nmea_mask;
	bool enable_pos_report;

	int listen_fd;
	int clients[SPX_GNSS_MAX_CLIENTS];
	size_t num_clients;

	struct spx_qmi_loc_client loc;
	struct spx_gnss_session session;
	struct spx_gnss_fix fix;
};

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int sig)
{
	(void)sig;
	g_stop_requested = 1;
}

static int install_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	/* Intentionally omit SA_RESTART so poll() wakes promptly on signal. */
	if (sigaction(SIGINT, &sa, NULL) != 0 ||
	    sigaction(SIGTERM, &sa, NULL) != 0 ||
	    sigaction(SIGHUP, &sa, NULL) != 0)
		return -1;
	signal(SIGPIPE, SIG_IGN);
	return 0;
}

static int set_nonblock_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
	flags = fcntl(fd, F_GETFD, 0);
	if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
		return -1;
	return 0;
}

static int open_unix_listener(const char *path, mode_t mode)
{
	struct sockaddr_un addr;
	int fd;

	if (!path || strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "spx-gnss: invalid socket path\n");
		return -1;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("spx-gnss: socket(AF_UNIX)");
		return -1;
	}
	if (set_nonblock_cloexec(fd) != 0) {
		perror("spx-gnss: fcntl");
		close(fd);
		return -1;
	}

	unlink(path);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, path, strlen(path) + 1u);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("spx-gnss: bind");
		close(fd);
		return -1;
	}
	if (chmod(path, mode) != 0) {
		perror("spx-gnss: chmod");
		close(fd);
		unlink(path);
		return -1;
	}
	if (listen(fd, 16) != 0) {
		perror("spx-gnss: listen");
		close(fd);
		unlink(path);
		return -1;
	}
	return fd;
}

static int daemon_start_loc(void *ctx, uint8_t session_id, uint32_t interval_ms)
{
	struct daemon_state *st = (struct daemon_state *)ctx;
	struct spx_qmi_result res;
	struct spx_qmi_loc_start_params start_p;
	uint64_t event_mask = SPX_QMI_LOC_EVENT_NMEA;
	int rc;

	if (spx_qmi_loc_open(&st->loc, SPX_GNSS_QMI_TIMEOUT_MS) != 0) {
		fprintf(stderr, "spx-gnss: QRTR lookup for QMI_SERVICE_LOC (0x%02x) failed\n",
			SPX_QMI_SERVICE_LOC);
		return -1;
	}

	/* Surface Pro X MPSS may boot with MI engine lock active; clear it so
	 * mobile-initiated GNSS sessions do not stall with PHONE_OFFLINE (7).
	 */
	rc = spx_qmi_loc_set_engine_lock(&st->loc, SPX_QMI_LOC_LOCK_NONE,
					 SPX_GNSS_QMI_TIMEOUT_MS, &res);
	if (rc != 0) {
		fprintf(stderr,
			"spx-gnss: QMI_LOC_SET_ENGINE_LOCK(NONE) warning (rc=%d result=%u error=%u)\n",
			rc, res.result, res.error);
	}

	/* Set NMEA sentence types (warn and continue if unsupported by firmware). */
	rc = spx_qmi_loc_set_nmea_types(&st->loc, st->nmea_mask,
					SPX_GNSS_QMI_TIMEOUT_MS, &res);
	if (rc < 0) {
		fprintf(stderr, "spx-gnss: QMI_LOC_SET_NMEA_TYPES transport failed\n");
		spx_qmi_loc_close(&st->loc);
		return -1;
	}
	if (rc > 0) {
		fprintf(stderr,
			"spx-gnss: QMI_LOC_SET_NMEA_TYPES returned result=%u error=%u (continuing)\n",
			res.result, res.error);
	}

	if (st->enable_pos_report || st->mode == MODE_JSON ||
	    st->mode == MODE_BOTH)
		event_mask |= SPX_QMI_LOC_EVENT_POSITION_REPORT;

	rc = spx_qmi_loc_reg_events(&st->loc, event_mask,
				    SPX_GNSS_QMI_TIMEOUT_MS, &res);
	if (rc != 0) {
		fprintf(stderr,
			"spx-gnss: QMI_LOC_REG_EVENTS failed (rc=%d result=%u error=%u)\n",
			rc, res.result, res.error);
		spx_qmi_loc_close(&st->loc);
		return -1;
	}

	memset(&start_p, 0, sizeof(start_p));
	start_p.session_id = session_id;
	start_p.has_intermediate_report_state = true;
	start_p.intermediate_report_state = SPX_QMI_LOC_INTERMEDIATE_ENABLE;
	start_p.has_min_interval_ms = true;
	start_p.min_interval_ms = interval_ms;

	rc = spx_qmi_loc_start(&st->loc, &start_p, SPX_GNSS_QMI_TIMEOUT_MS, &res);
	if (rc != 0) {
		fprintf(stderr,
			"spx-gnss: QMI_LOC_START failed (rc=%d result=%u error=%u)\n",
			rc, res.result, res.error);
		spx_qmi_loc_close(&st->loc);
		return -1;
	}

	spx_gnss_fix_init(&st->fix);
	fprintf(stderr,
		"spx-gnss: LOC session %u started (node=%u port=%u interval=%ums)\n",
		(unsigned)session_id, st->loc.service_node,
		st->loc.service_port, interval_ms);
	return 0;
}

static int daemon_stop_loc(void *ctx, uint8_t session_id)
{
	struct daemon_state *st = (struct daemon_state *)ctx;
	struct spx_qmi_result res;
	int rc = 0;

	if (st->loc.connected && st->loc.fd >= 0) {
		rc = spx_qmi_loc_stop(&st->loc, session_id,
				      SPX_GNSS_QMI_TIMEOUT_MS, &res);
		if (rc != 0) {
			fprintf(stderr,
				"spx-gnss: QMI_LOC_STOP warning (rc=%d result=%u error=%u)\n",
				rc, res.result, res.error);
		} else {
			fprintf(stderr, "spx-gnss: LOC session %u stopped\n",
				(unsigned)session_id);
		}
	}
	spx_qmi_loc_close(&st->loc);
	return rc;
}

static void remove_client_at(struct daemon_state *st, size_t idx)
{
	if (idx >= st->num_clients)
		return;
	close(st->clients[idx]);
	st->clients[idx] = st->clients[st->num_clients - 1u];
	st->num_clients--;
	(void)spx_gnss_session_on_client_disconnect(&st->session);
}

static void broadcast_line(struct daemon_state *st, const char *data, size_t len)
{
	size_t i = 0;

	while (i < st->num_clients) {
		ssize_t n = send(st->clients[i], data, len,
				 MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n != (ssize_t)len) {
			remove_client_at(st, i);
			continue;
		}
		i++;
	}
}

static void accept_clients(struct daemon_state *st)
{
	for (;;) {
		int cfd = accept(st->listen_fd, NULL, NULL);
		if (cfd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (st->num_clients >= SPX_GNSS_MAX_CLIENTS ||
		    set_nonblock_cloexec(cfd) != 0) {
			close(cfd);
			continue;
		}
		st->clients[st->num_clients++] = cfd;
		(void)spx_gnss_session_on_client_connect(&st->session);
	}
}

static void handle_qmi_packet(struct daemon_state *st, const uint8_t *pkt,
			      size_t len)
{
	uint16_t msg_id;

	if (len < SPX_QMI_HEADER_LEN || pkt[0] != SPX_QMI_FLAG_INDICATION)
		return;

	msg_id = (uint16_t)pkt[3] | ((uint16_t)pkt[4] << 8);

	if (msg_id == SPX_QMI_LOC_EVENT_NMEA_IND) {
		char nmea[SPX_QMI_LOC_MAX_NMEA_LEN + 4u];
		size_t nlen = 0;
		struct spx_nmea_sentence parsed;

		if (spx_qmi_loc_decode_nmea_ind(pkt, len, nmea,
						sizeof(nmea) - 2u, &nlen) != 0)
			return;
		if (!spx_nmea_verify_checksum(nmea, nlen))
			return;

		bool parsed_ok = (spx_nmea_parse(nmea, nlen, &parsed) == 0);
		if (parsed_ok)
			spx_gnss_fix_apply_nmea(&st->fix, &parsed);

		if (st->mode == MODE_NMEA || st->mode == MODE_BOTH) {
			nmea[nlen] = '\r';
			nmea[nlen + 1u] = '\n';
			broadcast_line(st, nmea, nlen + 2u);
		}
		if ((st->mode == MODE_JSON || st->mode == MODE_BOTH) &&
		    !st->enable_pos_report && parsed_ok &&
		    (parsed.type == SPX_NMEA_GGA ||
		     parsed.type == SPX_NMEA_RMC)) {
			char json[512];
			size_t jlen = spx_gnss_fix_format_json(&st->fix, json,
							       sizeof(json));
			if (jlen > 0)
				broadcast_line(st, json, jlen);
		}
	} else if (msg_id == SPX_QMI_LOC_EVENT_POSITION_REPORT_IND) {
		struct spx_qmi_loc_position pos;

		if (spx_qmi_loc_decode_position_ind(pkt, len, &pos) != 0)
			return;
		spx_gnss_fix_apply_position(&st->fix, &pos);
		if (st->mode == MODE_JSON || st->mode == MODE_BOTH) {
			char json[512];
			size_t jlen = spx_gnss_fix_format_json(&st->fix, json,
							       sizeof(json));
			if (jlen > 0)
				broadcast_line(st, json, jlen);
		}
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  --socket PATH              Unix socket path (default: %s)\n"
		"  --socket-mode OCTAL        Socket permissions (default: 0660)\n"
		"  --mode nmea|json|both      Client output format (default: nmea)\n"
		"  --interval MS              Min fix interval ms (default: %u)\n"
		"  --session-id N             QMI LOC session ID 0..255 (default: %u)\n"
		"  --enable-position-report   Also subscribe to QMI_LOC_EVENT_POSITION_REPORT_IND\n"
		"  --help                     Show this help\n",
		prog, SPX_GNSS_DEFAULT_SOCK, SPX_GNSS_DEFAULT_INTERVAL,
		SPX_GNSS_DEFAULT_SESSION_ID);
}

static bool parse_u32_arg(const char *s, int base, uint32_t *out)
{
	char *end = NULL;
	unsigned long v;

	if (!s || *s == '\0')
		return false;
	errno = 0;
	v = strtoul(s, &end, base);
	if (errno != 0 || !end || *end != '\0' || v > UINT32_MAX)
		return false;
	*out = (uint32_t)v;
	return true;
}

static int parse_args(int argc, char **argv, struct daemon_state *st)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 ||
		    strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return 1;
		} else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
			st->sock_path = argv[++i];
		} else if (strcmp(argv[i], "--socket-mode") == 0 &&
			   i + 1 < argc) {
			uint32_t m = 0;
			if (!parse_u32_arg(argv[++i], 8, &m) || m > 0777u) {
				fprintf(stderr, "spx-gnss: invalid --socket-mode\n");
				return -1;
			}
			st->sock_mode = (mode_t)m;
		} else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
			const char *m = argv[++i];
			if (strcmp(m, "nmea") == 0)
				st->mode = MODE_NMEA;
			else if (strcmp(m, "json") == 0)
				st->mode = MODE_JSON;
			else if (strcmp(m, "both") == 0)
				st->mode = MODE_BOTH;
			else {
				fprintf(stderr, "spx-gnss: invalid --mode %s\n", m);
				return -1;
			}
		} else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
			uint32_t ms = 0;
			if (!parse_u32_arg(argv[++i], 10, &ms) || ms < 100u ||
			    ms > 60000u) {
				fprintf(stderr, "spx-gnss: --interval must be 100..60000\n");
				return -1;
			}
			st->interval_ms = ms;
		} else if (strcmp(argv[i], "--session-id") == 0 &&
			   i + 1 < argc) {
			uint32_t sid = 0;
			if (!parse_u32_arg(argv[++i], 10, &sid) || sid > 255u) {
				fprintf(stderr, "spx-gnss: --session-id must be 0..255\n");
				return -1;
			}
			st->session_id = (uint8_t)sid;
		} else if (strcmp(argv[i], "--enable-position-report") == 0) {
			st->enable_pos_report = true;
		} else {
			fprintf(stderr, "spx-gnss: unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct daemon_state st;
	struct spx_gnss_session_ops ops = {
		.start_loc = daemon_start_loc,
		.stop_loc = daemon_stop_loc,
	};
	int arg_rc;

	memset(&st, 0, sizeof(st));
	st.sock_path = SPX_GNSS_DEFAULT_SOCK;
	st.sock_mode = SPX_GNSS_DEFAULT_SOCK_MODE;
	st.mode = MODE_NMEA;
	st.session_id = SPX_GNSS_DEFAULT_SESSION_ID;
	st.interval_ms = SPX_GNSS_DEFAULT_INTERVAL;
	st.nmea_mask = SPX_QMI_LOC_NMEA_DEFAULT;
	st.listen_fd = -1;
	spx_qmi_loc_client_init(&st.loc);
	spx_gnss_fix_init(&st.fix);

	arg_rc = parse_args(argc, argv, &st);
	if (arg_rc != 0)
		return arg_rc > 0 ? 0 : 2;

	if (install_signals() != 0) {
		perror("spx-gnss: sigaction");
		return 1;
	}

	spx_gnss_session_init(&st.session, st.session_id, st.interval_ms, true,
			      &ops, &st);

	st.listen_fd = open_unix_listener(st.sock_path, st.sock_mode);
	if (st.listen_fd < 0)
		return 1;

	while (!g_stop_requested) {
		struct pollfd pfds[2u + SPX_GNSS_MAX_CLIENTS];
		nfds_t nfds = 0;
		size_t loc_idx = (size_t)-1;
		size_t client_start;
		int timeout_ms = -1; /* Idle: sleep forever until client or signal. */
		int rc;

		pfds[nfds].fd = st.listen_fd;
		pfds[nfds].events = POLLIN;
		pfds[nfds].revents = 0;
		nfds++;

		if (st.loc.fd >= 0 && st.loc.connected) {
			loc_idx = nfds;
			pfds[nfds].fd = st.loc.fd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;
		} else if (st.num_clients > 0) {
			/* Clients waiting while LOC is unavailable: bounded retry. */
			timeout_ms = SPX_GNSS_RETRY_MS;
		}

		client_start = nfds;
		size_t polled_clients = st.num_clients;
		for (size_t i = 0; i < polled_clients; i++) {
			pfds[nfds].fd = st.clients[i];
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;
		}

		rc = poll(pfds, nfds, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			perror("spx-gnss: poll");
			break;
		}
		if (rc == 0) {
			if (st.num_clients > 0 && !st.session.active)
				(void)spx_gnss_session_on_loc_ready(&st.session);
			continue;
		}

		/* Check disconnects on polled clients BEFORE accepting new ones,
		 * so pfds[client_start + ci] stays aligned 1:1 with st.clients[ci].
		 */
		for (size_t i = polled_clients; i > 0; i--) {
			size_t ci = i - 1u;
			short rev = pfds[client_start + ci].revents;
			if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
				remove_client_at(&st, ci);
			} else if (rev & POLLIN) {
				char sink[64];
				ssize_t n = recv(st.clients[ci], sink,
						 sizeof(sink), MSG_DONTWAIT);
				if (n == 0 ||
				    (n < 0 && errno != EAGAIN &&
				     errno != EWOULDBLOCK && errno != EINTR)) {
					remove_client_at(&st, ci);
				}
			}
		}

		if (pfds[0].revents & POLLIN)
			accept_clients(&st);

		if (loc_idx != (size_t)-1 && st.loc.fd >= 0) {
			short rev = pfds[loc_idx].revents;
			if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
				fprintf(stderr, "spx-gnss: QRTR socket error\n");
				spx_qmi_loc_close(&st.loc);
				spx_gnss_session_on_loc_lost(&st.session);
			} else if (rev & POLLIN) {
				uint8_t rx[4096];
				size_t rx_len = 0;
				struct spx_qrtr_service_info ctrl;
				int r = spx_qmi_loc_recv(&st.loc, rx,
							 sizeof(rx), &rx_len,
							 &ctrl);
				if (r == 1) {
					handle_qmi_packet(&st, rx, rx_len);
				} else if (r == 2) {
					if (spx_qrtr_match_del_server(
						    &ctrl, SPX_QMI_SERVICE_LOC,
						    st.loc.service_node,
						    st.loc.service_port)) {
						fprintf(stderr,
							"spx-gnss: QMI_SERVICE_LOC disappeared\n");
						spx_qmi_loc_close(&st.loc);
						spx_gnss_session_on_loc_lost(
							&st.session);
					}
				} else if (r < 0) {
					spx_qmi_loc_close(&st.loc);
					spx_gnss_session_on_loc_lost(
						&st.session);
				}
			}
		}
	}

	(void)spx_gnss_session_shutdown(&st.session);
	for (size_t i = 0; i < st.num_clients; i++)
		close(st.clients[i]);
	if (st.listen_fd >= 0)
		close(st.listen_fd);
	unlink(st.sock_path);
	return 0;
}
