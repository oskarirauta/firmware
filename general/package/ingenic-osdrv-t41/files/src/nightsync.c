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
 * It also supplies the light reading, on the cameras where majestic cannot get
 * one. majestic has two monitors: a hardware one on lightSensorPin, which works
 * here because the driver's device is symlinked to the name it expects, and a
 * software one that compares its isp_again metric against minThreshold and
 * maxThreshold. On Ingenic the software one is blind - majestic imports no ISP
 * gain function from libimp at all and never reads /proc/jz/isp, so isp_again
 * stays -1 - and a camera built without a photoresistor has no hardware sensor
 * either, which leaves it with no automatic day/night at all.
 *
 * The number itself is not missing, only majestic's route to it: the ISP
 * publishes its own analog gain in /proc/jz/isp as AeAGain, and the web UI's
 * light readout is already fed from exactly there. So where majestic has no
 * sensor to read, this reads that and calls /night/on and /night/off - the same
 * shape as OpenIPC's own autonight, which reads an ADC and calls the same two
 * endpoints. Thresholds, hysteresis and the delay between switches all come
 * from majestic's configuration, so they are still edited in one place.
 *
 * It stays out of the way whenever majestic can do it: with lightMonitor off
 * nothing is automatic and the buttons are in charge, and with a lightSensorPin
 * configured majestic's own hardware monitor owns it.
 *
 * The grey conversion itself is not done here either. It goes through the
 * majestic plugin, which is where OpenIPC keeps per-SoC ISP knowledge, so there
 * is one implementation rather than two:
 *
 *     majestic  --(decides, drives the pins, the night endpoints)-->  night_enabled
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
#include <dirent.h>
#include <limits.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define HTTP_PORT	80	/* majestic's own web server */
#define PLUGIN_PORT	4000	/* majestic's plugin command socket */
#define POLL_MS		1000
#define CONFIG		"/etc/majestic.yaml"
#define ISP_PROC	"/proc/jz/isp"

/* A second is the compromise. The Preview button has to feel immediate - the
 * whole reason for this is that reaching for a shell instead is impractical,
 * and unsafe if the camera is being watched from a moving car - while a poll
 * costs one loopback request, so a faster one would buy nothing a person can
 * see. */

/* majestic's configuration, re-read as it goes so a change made in the web UI
 * takes effect without a restart. Only the nightMode block is of interest, and
 * the file is small, so a scanner for the two-level layout majestic writes is
 * enough - no YAML parser, and nothing here writes the file.
 *
 * A key that is absent stays absent rather than acquiring a default: majestic's
 * own built-in defaults are not visible from here, and inventing thresholds is
 * how a camera ends up deciding it is night at noon. Without thresholds this
 * simply does not automate, and says so once. */
struct night_config {
	int light_monitor;
	int have_sensor_pin;
	int min_threshold;	/* back to day below this */
	int max_threshold;	/* to night above this */
	int monitor_delay;	/* seconds; -1 if unset */
};

static int yaml_bool(const char *v) {
	return !strncmp(v, "true", 4);
}

static void read_config(struct night_config *c) {
	memset(c, 0, sizeof(*c));
	c->min_threshold = c->max_threshold = c->monitor_delay = -1;

	FILE *f = fopen(CONFIG, "r");
	if (!f) {
		return;
	}

	char line[256];
	int in_night = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] != ' ' && line[0] != '\t') {
			in_night = !strncmp(line, "nightMode:", 10);
			continue;
		}

		if (!in_night) {
			continue;
		}

		char *key = line;
		while (*key == ' ' || *key == '\t') {
			key++;
		}

		char *sep = strchr(key, ':');
		if (!sep) {
			continue;
		}

		*sep = 0;
		char *val = sep + 1;
		while (*val == ' ') {
			val++;
		}

		if (!strcmp(key, "lightMonitor")) {
			c->light_monitor = yaml_bool(val);
		} else if (!strcmp(key, "lightSensorPin")) {
			c->have_sensor_pin = 1;
		} else if (!strcmp(key, "minThreshold")) {
			c->min_threshold = atoi(val);
		} else if (!strcmp(key, "maxThreshold")) {
			c->max_threshold = atoi(val);
		} else if (!strcmp(key, "monitorDelay")) {
			c->monitor_delay = atoi(val);
		}
	}

	fclose(f);
}

/* The ISP's own analog gain, the same number the web UI shows and the same one
 * majestic's thresholds are documented against. It rises as the scene darkens.
 * The file it lives in is named after the running ISP, so the directory is
 * scanned rather than guessed, and "AeAGain " is matched with its trailing
 * space so the neighbouring AeAGainManualMode line cannot be read instead. */
static int read_gain(void) {
	DIR *d = opendir(ISP_PROC);
	if (!d) {
		return -1;
	}

	struct dirent *e;
	int gain = -1;
	while (gain < 0 && (e = readdir(d))) {
		char path[sizeof(ISP_PROC) + 1 + NAME_MAX + 1];
		snprintf(path, sizeof(path), ISP_PROC "/%s", e->d_name);

		FILE *f = fopen(path, "r");
		if (!f) {
			continue;
		}

		char line[128];
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "AeAGain ", 8)) {
				continue;
			}

			char *v = strchr(line, ':');
			if (v) {
				gain = atoi(v + 1);
			}
			break;
		}

		fclose(f);
	}

	closedir(d);
	return gain;
}

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

/* Ask majestic to switch, rather than switching anything here. It then does
 * everything it normally does - the IR-cut pins, the illuminator, its own state
 * and the metric this program reads back - so the result is identical to
 * someone pressing the button on the Preview page. */
static int request_night(int on) {
	char request[128], reply[512];
	snprintf(request, sizeof(request),
		"GET /night/%s HTTP/1.0\r\nHost: 127.0.0.1\r\n"
		"Connection: close\r\n\r\n", on ? "on" : "off");
	return transact(HTTP_PORT, request, reply, sizeof(reply));
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
	int no_thresholds = 0;
	time_t last_switch = 0;

	for (;;) {
		int night = read_night_state();

		/* majestic not up, or not answering yet. Not an error: this starts
		 * alongside it and the camera reboots. Say nothing and wait. */
		if (night < 0) {
			applied = -1;	/* re-apply once it comes back */
			usleep(POLL_MS * 1000);
			continue;
		}

		/* Supply the decision only where majestic cannot make it. */
		struct night_config cfg;
		read_config(&cfg);

		if (cfg.light_monitor && !cfg.have_sensor_pin) {
			if (cfg.min_threshold < 0 || cfg.max_threshold < 0) {
				if (!no_thresholds) {
					syslog(LOG_WARNING,
						"lightMonitor is on with no lightSensorPin, so the "
						"light level is read here - but nightMode.minThreshold "
						"and nightMode.maxThreshold are not set, so nothing "
						"will switch. Set them in the web UI.");
					no_thresholds = 1;
				}
			} else {
				no_thresholds = 0;

				int gain = read_gain();
				/* monitorDelay is majestic's own name for how long to leave a
				 * switch alone. It matters more here than it looks: this gain
				 * comes from the ISP, so switching changes the very number the
				 * decision was made from, and without a pause the picture
				 * hunts between colour and grey. */
				int delay = cfg.monitor_delay > 0 ? cfg.monitor_delay : 0;
				time_t now = time(NULL);

				if (gain >= 0 && now - last_switch >= delay) {
					int want = night;
					if (gain > cfg.max_threshold) {
						want = 1;
					} else if (gain < cfg.min_threshold) {
						want = 0;
					}

					if (want != night && !request_night(want)) {
						syslog(LOG_INFO, "gain %d -> %s", gain,
							want ? "night" : "day");
						last_switch = now;
						night = want;
					}
				}
			}
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
