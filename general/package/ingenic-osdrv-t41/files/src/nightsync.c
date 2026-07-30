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
 * one - which is most of them here. majestic has two monitors. The hardware one
 * watches lightSensorPin, and that is a GPIO: the streamer's own schema calls it
 * "GPIO pin for light sensor", so it wants a digital day/night signal. The
 * software one compares its isp_again metric against minThreshold and
 * maxThreshold, and on Ingenic that is blind - majestic imports no ISP gain
 * function from libimp at all and never opens /proc/jz/isp, so isp_again stays
 * -1.
 *
 * majestic does read an ADC, but only to publish it: adcReadout is a value for
 * the web UI to show, not an input to the night decision. So a camera whose
 * light sensor is a photoresistor on the SADC has nothing majestic can decide
 * from, and neither has one with no sensor at all.
 *
 * Both numbers exist, only majestic's route to them does not, so this reads
 * whichever the camera has and calls /night/on and /night/off. That is the same
 * shape as OpenIPC's own autonight, which reads the same ADC and calls the same
 * two endpoints. Thresholds, hysteresis and the delay between switches all come
 * from majestic's configuration, so they are still edited in one place.
 *
 *   adc    a photoresistor on SADC AUX0, when isp.adcReadout says this camera
 *          has one worth reading. Preferred where it exists: it measures the
 *          room, independently of the pipeline.
 *   gain   the ISP's own analog gain from /proc/jz/isp, which is what feeds the
 *          web UI's light readout. Needs no extra component, so it is what is
 *          left when there is no photoresistor. Note that it is a measurement of
 *          the pipeline, so switching moves it - which is what monitorDelay is
 *          for below.
 *
 * Both read higher as it gets darker, so one threshold pair serves either and
 * the comparison never changes direction. A board wired the other way round is
 * normalised by the ADC driver's own invert parameter, not here.
 *
 * It stays out of the way whenever majestic can do the job: with lightMonitor
 * off nothing is automatic and the buttons are in charge, and with a
 * lightSensorPin configured majestic's own hardware monitor owns it.
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
 * The plugin is preferred but not required, and that is deliberate. Since
 * 2025-11-21 majestic-plugins is under the Prosperity Public License 3.0.0,
 * which is free for noncommercial use only, so a camera must not need it
 * installed for its Night button to work. Where it is there - and where
 * ".system.plugins true" is set, without which majestic never loads it - the
 * command goes through it, because that is where OpenIPC keeps per-SoC ISP
 * knowledge. Where it is not, the same driver parameter is written directly.
 * The duplication is small and buys the feature its independence.
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
	int adc_readout;	/* isp.adcReadout: this camera has a photoresistor */
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
	enum { OTHER, NIGHT, ISP } section = OTHER;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] != ' ' && line[0] != '\t') {
			section = !strncmp(line, "nightMode:", 10) ? NIGHT :
				  !strncmp(line, "isp:", 4) ? ISP : OTHER;
			continue;
		}

		if (section == OTHER) {
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

		if (section == ISP) {
			if (!strcmp(key, "adcReadout")) {
				c->adc_readout = yaml_bool(val);
			}

			continue;
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

/* The photoresistor, when there is one. A binary unsigned long rather than text,
 * and read fresh each time - the driver converts on read. Both device names are
 * tried: the driver registers itself as ingenic_adc_aux_%d, while the older
 * vendor name is what majestic opens, and load_ingenic symlinks one to the
 * other, so either may be the real one depending on boot order. */
static int read_adc(void) {
	static const char *devices[] = {
		"/dev/ingenic_adc_aux_0",
		"/dev/jz_adc_aux_0",
		NULL
	};

	for (int i = 0; devices[i]; i++) {
		FILE *f = fopen(devices[i], "rb");
		if (!f) {
			continue;
		}

		unsigned long value = 0;
		int got = fread(&value, sizeof(value), 1, f) == 1;
		fclose(f);

		if (got) {
			return (int)value;
		}
	}

	return -1;
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

/* The same switch the plugin writes, written here when there is no plugin to
 * write it. The driver is named after its SoC, so the module directory is found
 * rather than assumed. */
static int write_isp_daynight(int night) {
	DIR *d = opendir("/sys/module");
	if (!d) {
		return -1;
	}

	struct dirent *e;
	int done = -1;
	while (done < 0 && (e = readdir(d))) {
		if (strncmp(e->d_name, "tx_isp", 6) && strncmp(e->d_name, "tx-isp", 6)) {
			continue;
		}

		char path[sizeof("/sys/module//parameters/daynight") + NAME_MAX];
		snprintf(path, sizeof(path), "/sys/module/%s/parameters/daynight",
			e->d_name);

		FILE *f = fopen(path, "w");
		if (!f) {
			continue;
		}

		done = fprintf(f, "%d", night) > 0 ? 0 : -1;
		fclose(f);
	}

	closedir(d);
	return done;
}

static int set_blackwhite(int night, char *reply, size_t len) {
	char request[32];
	snprintf(request, sizeof(request), "blackwhite %d\n", night);
	if (!transact(PLUGIN_PORT, request, reply, len)) {
		return 0;
	}

	if (write_isp_daynight(night)) {
		return -1;
	}

	snprintf(reply, len, "set directly (no plugin)");
	return 0;
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

				/* Which sensor this camera actually has. adcReadout is
				 * majestic's own way of saying "there is a photoresistor
				 * here worth reading", so it selects the source and the
				 * choice stays in the web UI with everything else. It is
				 * not guessed from whether the device opens: an unfitted
				 * photoresistor still opens and still returns a number -
				 * a steady near-zero, measured at 4 to 35 units on one
				 * here - which would read as broad daylight forever. */
				int gain = cfg.adc_readout ? read_adc() : read_gain();
				const char *source = cfg.adc_readout ? "adc" : "gain";
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
						syslog(LOG_INFO, "%s %d -> %s", source, gain,
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
						"cannot switch the ISP: no plugin on port %d "
						"and no tx_isp daynight parameter either",
						PLUGIN_PORT);
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
