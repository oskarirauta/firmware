/*
 * nightsync - give majestic's night mode the half of itself that Ingenic lacks.
 *
 * majestic owns day/night on this camera and needs to keep owning it: it holds
 * the thresholds, the monitor delay and the IR-cut and illuminator pins, all
 * configured from its own web UI, and it publishes the switch on /night/on,
 * /night/off and /night/toggle - which is what the Preview page's buttons call.
 * Nothing here decides anything, and that is the point. A second owner would
 * fight the first, and the settings people actually edit live over there.
 *
 * One piece of the switch is missing on these parts. majestic has no ingenic
 * implementation of the grey conversion - it says so itself, "Not implemented
 * for platform ingenic" - so the filter moves and the lamp comes on while the
 * picture stays in colour. There is no hook to fill that in from: majestic
 * offers no callback on a night change. So this watches the state it already
 * publishes, and applies the missing half whenever it changes.
 *
 * The grey conversion itself is not done here either. It goes through the
 * majestic plugin, which is where OpenIPC keeps per-SoC ISP knowledge, so there
 * is one implementation rather than two:
 *
 *     majestic  --(decides, drives the pins, /night/*)-->  night_enabled
 *                                                              |
 *                                                          nightsync
 *                                                              |
 *                                        ingenic.so blackwhite --> ISP driver
 *
 * Requires ".system.plugins true" in majestic's configuration; without it the
 * plugin is never loaded and the socket below is never opened. That is reported
 * once rather than every second, because it is a configuration mistake to fix,
 * not a fault to watch scroll past.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#define HTTP_PORT	80	/* majestic's own web server */
#define PLUGIN_PORT	4000	/* majestic's plugin command socket */
#define POLL_MS		1000

/* A second is the compromise. The Preview button has to feel immediate - the
 * whole reason for this is that reaching for a shell instead is impractical,
 * and unsafe if the camera is being watched from a moving car - while a poll
 * costs one loopback request, so a faster one would buy nothing a person can
 * see. */

static int connect_local(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(fd);
		return -1;
	}

	return fd;
}

static int transact(int port, const char *request, char *reply, size_t len) {
	int fd = connect_local(port);
	if (fd < 0) {
		return -1;
	}

	size_t want = strlen(request);
	int ok = write(fd, request, want) == (ssize_t)want;

	ssize_t got = 0;
	if (ok) {
		got = read(fd, reply, len - 1);
	}

	close(fd);
	if (!ok || got < 0) {
		return -1;
	}

	reply[got] = 0;
	return 0;
}

/* majestic's own view of whether night mode is on. Asking it rather than
 * keeping a copy means a switch made any way at all - the Preview button, the
 * light monitor, a /night/on from somewhere else - is seen the same. */
static int read_night_state(void) {
	char reply[512];
	if (transact(HTTP_PORT,
		"GET /metrics/night?value=night_enabled HTTP/1.0\r\n"
		"Host: 127.0.0.1\r\n"
		"Connection: close\r\n\r\n", reply, sizeof(reply))) {
		return -1;
	}

	/* Skip the headers; the body is the bare number. */
	char *body = strstr(reply, "\r\n\r\n");
	if (!body) {
		return -1;
	}

	body += 4;
	while (*body == '\r' || *body == '\n' || *body == ' ') {
		body++;
	}

	if (*body < '0' || *body > '9') {
		return -1;
	}

	return atoi(body) > 0;
}

static int set_blackwhite(int night, char *reply, size_t len) {
	char request[32];
	snprintf(request, sizeof(request), "blackwhite %d\n", night);
	return transact(PLUGIN_PORT, request, reply, len);
}

int main(void) {
	openlog("nightsync", LOG_PID, LOG_DAEMON);

	int applied = -1;		/* nothing applied yet */
	int complained = 0;

	for (;;) {
		int night = read_night_state();

		/* majestic not up, or not answering yet. Not an error: this starts
		 * alongside it and the camera reboots. Say nothing and wait. */
		if (night < 0) {
			applied = -1;	/* re-apply once it comes back */
			usleep(POLL_MS * 1000);
			continue;
		}

		if (night != applied) {
			char reply[512];
			if (set_blackwhite(night, reply, sizeof(reply))) {
				if (!complained) {
					syslog(LOG_WARNING,
						"cannot reach the plugin on port %d - is "
						"\".system.plugins true\" set? (%s)",
						PLUGIN_PORT, strerror(errno));
					complained = 1;
				}
			} else {
				syslog(LOG_INFO, "night %d: %s", night, reply);
				applied = night;
				complained = 0;
			}
		}

		usleep(POLL_MS * 1000);
	}

	return 0;
}
