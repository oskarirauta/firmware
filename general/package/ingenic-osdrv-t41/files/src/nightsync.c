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
 * WHAT ENABLES IT IS THE THRESHOLDS, NOT lightMonitor - and that is not a
 * stylistic choice, it is forced. Setting lightMonitor starts majestic's own
 * software monitor, and on Ingenic that monitor is blind: isp_again is always
 * -1, which is below any minThreshold, so it concludes the scene is bright and
 * drives the camera back to day about a second after anything sets night. The
 * result is a camera that flips between day and night every few seconds and
 * pulses the IR-cut solenoid each time. So lightMonitor must stay off here, and
 * a pair of thresholds is what says automatic switching is wanted.
 *
 * That leaves the Preview buttons enabled, which majestic disables whenever
 * lightMonitor is set - so on this platform automatic and manual are both
 * available at once, which they are not otherwise. A switch made by hand is
 * noticed and left alone for up to MANUAL_HOLD, which is a separate and much
 * longer period than monitorDelay on purpose: monitorDelay says how long a
 * change in the light has to last before it is believed, while this says how
 * long a person gets to look at something after asking to. Someone who presses
 * the button wants to see the scene now, and having it undone a few seconds
 * later would defeat the reason the button is there.
 *
 * "Up to", because the hold only protects a choice the light disagrees with. As
 * soon as they agree it is dropped - switching to night by hand on a dark
 * evening is what the light wanted anyway, and should not stop automation at
 * all.
 *
 * It still stays out of the way where majestic can do the job: a configured
 * lightSensorPin leaves majestic's own hardware monitor in charge.
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
 * CONFIGURATION - everything this reads, in one place.
 *
 * All of it belongs to majestic and is edited in its web UI, with one
 * exception noted at the end.
 *
 *   nightMode.minThreshold   back to day below this
 *   nightMode.maxThreshold   to night above this
 *                            Setting BOTH is what enables automatic switching.
 *                            Compared against the light level, which reads
 *                            higher as it gets darker. There are no defaults:
 *                            an invented one is how a camera decides it is
 *                            night at noon.
 *   nightMode.monitorDelay   how long the light must STAY past a threshold
 *                            before it is believed. Not a cooldown - that
 *                            would let a torch swept across the lens switch
 *                            the mode and only then wait.
 *   nightMode.colorToGray    whether night mode desaturates at all. Off means
 *                            IR-cut and lamp without losing colour.
 *   nightMode.lightMonitor   MUST STAY OFF on this SoC. It starts majestic's
 *                            own monitor, which compares an isp_again it never
 *                            fills - always -1, below any minThreshold - so it
 *                            settles on day and holds it. With it on, this
 *                            program stands down entirely and says so once.
 *   nightMode.lightSensorPin a digital light sensor on a GPIO. Where one is
 *                            configured, majestic's hardware monitor owns the
 *                            decision and this program stays out of it.
 *   isp.adcReadout           says the camera has a photoresistor worth reading,
 *                            and selects it as the light source in place of the
 *                            ISP's gain. Which AUX channel it is on, and its
 *                            polarity, are board facts and live in the U-Boot
 *                            environment as adc_channel and adc_invert - see
 *                            load_ingenic, which aims the device link and sets
 *                            the driver parameter from them.
 *
 * And the one setting that is not majestic's, because majestic has no key for
 * it and an invented one would not survive - majestic rewrites its own file
 * when settings are saved and drops what it does not know:
 *
 *   nightsync_auto           in the U-Boot environment. The ONLY purpose is to
 *                            switch automatic day/night OFF on a camera that
 *                            should never change mode by itself:
 *
 *                                fw_setenv nightsync_auto off
 *
 *                            Absent means enabled, so a camera that has never
 *                            heard of it behaves normally - it is an off
 *                            switch and nothing else. "off", "0", "false" and
 *                            "no" all disable. Read once at startup, so it
 *                            takes a restart of this daemon to take effect,
 *                            and the log says so when it is in force. The
 *                            buttons keep working either way; only the
 *                            automatic switching stops. The web UI's
 *                            environment editor can set it, so it does not
 *                            need a shell.
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
#include <fcntl.h>
#include <sys/select.h>
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
#define DEFAULT_DELAY	10	/* seconds, only when monitorDelay is unset */
#define MANUAL_HOLD	90	/* seconds a switch made by hand is left alone */
#define PLACE_MS	400	/* how long the filter is left in night while placing it */

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
	int color_to_gray;	/* nightMode.colorToGray: grey the picture at night */
	int have_ircut;		/* nightMode.irCutPin1: a filter is configured */
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
	c->color_to_gray = 1;	/* majestic's own default */

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

		if (!strcmp(key, "irCutPin1")) {
			c->have_ircut = 1;
		} else if (!strcmp(key, "colorToGray")) {
			c->color_to_gray = yaml_bool(val);
		} else if (!strcmp(key, "lightMonitor")) {
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
		/* The compatibility name first, deliberately: load_ingenic points it
		 * at whichever AUX channel the board's sensor is on, so it carries the
		 * configuration. The driver's own name for channel 0 is the fallback
		 * for a system where that link was never made. */
		"/dev/jz_adc_aux_0",
		"/dev/ingenic_adc_aux_0",
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

/* An off switch, for a camera that should never change mode on its own. It
 * lives in the U-Boot environment rather than majestic's configuration because
 * majestic has no key for it - and inventing one there would not survive, since
 * majestic rewrites that file whenever settings are saved and drops what it
 * does not know. The environment is where this firmware already keeps per-unit
 * settings, and the web UI can edit it, so it is still reachable without a
 * shell. Read once: an enable flag is not something that changes while running,
 * and paying a fork every second for it would be silly.
 *
 * Absent means enabled, so a camera that has never heard of this behaves as
 * before. */
static int automation_enabled(void) {
	FILE *f = popen("fw_printenv -n nightsync_auto 2>/dev/null", "r");
	if (!f) {
		return 1;
	}

	char value[32] = "";
	if (!fgets(value, sizeof(value), f)) {
		value[0] = 0;
	}

	pclose(f);

	return !(!strncmp(value, "off", 3) || !strncmp(value, "0", 1) ||
		 !strncmp(value, "false", 5) || !strncmp(value, "no", 2));
}

/* Every socket gets a deadline, and this is not belt-and-braces: without one a
 * poll made while majestic is restarting can be accepted and then never
 * answered, and the read blocks for ever. The daemon stays alive - it is still
 * there in pidof - and simply never does anything again, which is a far worse
 * failure than dying would have been. Seen on hardware: day/night stopped
 * working after a majestic restart and stayed stopped. */
#define IO_TIMEOUT_S	3

static int connect_local(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}

	struct timeval tv = { .tv_sec = IO_TIMEOUT_S };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};

	/* connect() has its own way of hanging - a listening socket whose backlog
	 * is full leaves it waiting - so it is bounded separately. */
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}

	fd_set w;
	FD_ZERO(&w);
	FD_SET(fd, &w);
	struct timeval ctv = { .tv_sec = IO_TIMEOUT_S };

	if (select(fd + 1, NULL, &w, NULL, &ctv) != 1) {
		close(fd);
		return -1;
	}

	int err = 0;
	socklen_t len = sizeof(err);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) || err) {
		close(fd);
		return -1;
	}

	fcntl(fd, F_SETFL, flags);
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
	int warned_monitor = 0;
	int no_reading = 0;
	int requested = -1;		/* the last state asked of majestic */
	time_t hold_until = 0;		/* automation waits until this moment */
	int pending = -1;		/* what the light has been saying */
	time_t pending_since = 0;
	int automate = automation_enabled();

	if (!automate) {
		syslog(LOG_INFO, "automatic switching is off (nightsync_auto), "
			"the buttons still work");
	}

	for (;;) {
		int night = read_night_state();

		/* majestic not up, or not answering yet. Not an error: this starts
		 * alongside it and the camera reboots. Say nothing and wait. */
		if (night < 0) {
			applied = -1;	/* re-apply once it comes back */
			requested = -1;	/* and adopt whatever state it comes back in,
					 * rather than reading a restart as somebody
					 * pressing the button */

			/* A hold must not outlive the majestic it was protecting a
			 * choice in. The override itself is gone - majestic comes back
			 * in day mode with no memory of it - so a surviving hold would
			 * block automation while guarding nothing. */
			hold_until = 0;
			pending = -1;

			usleep(POLL_MS * 1000);
			continue;
		}

		/* Supply the decision only where majestic cannot make it. */
		struct night_config cfg;
		read_config(&cfg);

		/* Start from day, every time, and only then let the light argue.
		 *
		 * This is how the same thing works on cameras generally, and the reason
		 * is that it gives a known baseline: after a start the mode is not
		 * whatever happened to be left behind or reported, it is day, and any
		 * move away from it is one this program made and can account for. If it
		 * really is dark the light says so continuously and the mode changes as
		 * soon as the hysteresis is satisfied, which costs one monitorDelay and
		 * removes the ambiguity for good.
		 *
		 * Only when automation is running. With it switched off the state is
		 * somebody else's to choose and is left exactly as found. */
		if (requested < 0) {
			/*
			 * Put the IR-cut filter somewhere known, by driving a full
			 * transition rather than trusting the reported state.
			 *
			 * The filter is a latching solenoid: it stays wherever it was
			 * last pushed, including across a reflash, and nothing can
			 * read its position back. majestic only moves it when the mode
			 * *changes*, and at startup it believes it is already in day -
			 * so it drives nothing, and a camera whose previous firmware
			 * left the filter in the night position shows a red picture
			 * indefinitely with no indication why. The vendor firmware and
			 * thingino both place it at boot; the audible click at startup
			 * is exactly that, and its absence was the symptom.
			 *
			 * Requesting night and then day makes majestic perform a real
			 * transition, so the solenoid ends up in the day position
			 * whatever it believed. The cost is one extra pulse per boot
			 * and a brief red flash, against a picture that is otherwise
			 * permanently wrong.
			 *
			 * Only where a filter is actually configured - with no
			 * irCutPin1 there is nothing to place, and the flip would be
			 * pure noise. Done regardless of whether automation is on: this
			 * corrects the hardware to match what the software already
			 * believes, which is not the same as overriding a choice.
			 */
			if (cfg.have_ircut) {
				syslog(LOG_INFO, "placing the IR-cut filter in day");
				request_night(1);
				usleep(PLACE_MS * 1000);
				request_night(0);
				night = 0;
			} else if (automate && night) {
				request_night(0);
				night = 0;
			}

			requested = night;
			syslog(LOG_INFO, "starting in %s", night ? "night" : "day");
		}

		/* Somebody moved it, and it was not us - the Preview page's button, or
		 * a /night/ request from elsewhere. Adopt it and let it stand for the
		 * monitorDelay before automation is allowed to argue. That is what
		 * makes the button usable on a camera that is also switching itself:
		 * the whole point of having a button is being able to look at
		 * something now, without reaching for a shell. */
		if (night != requested) {
			/* Only worth saying, or holding, when automation is running -
			 * there is nothing to hold it against otherwise. */
			if (automate) {
				syslog(LOG_INFO, "switched to %s by hand, holding up to %d s",
					night ? "night" : "day", MANUAL_HOLD);
				hold_until = time(NULL) + MANUAL_HOLD;
			}

			requested = night;
		}

		/* lightMonitor on means majestic's own light monitor is running, so
		 * this one stands down - two of them deciding would fight, and the
		 * fight is visible: majestic wins about a second after anything sets
		 * night, and the camera flips every few seconds.
		 *
		 * That is a legitimate choice rather than a mistake, so it is stated
		 * once and not repeated. It is worth knowing what it costs, though.
		 * On this SoC majestic's software monitor compares an isp_again it
		 * never fills, which is always -1 and therefore below any minThreshold,
		 * so it settles on day and stays there. Its IR-cut and lamp settings
		 * still work - those are GPIOs and majestic drives them itself - and
		 * the grey conversion still follows if anything does reach night mode.
		 * What is lost is the switching: nothing will move it off day. */
		if (automate && cfg.light_monitor && !cfg.have_sensor_pin &&
		    !warned_monitor) {
			syslog(LOG_INFO,
				"standing down: nightMode.lightMonitor is on, so majestic's "
				"own monitor decides. Note that it cannot see the light on "
				"this SoC - its isp_again is always -1 - so it will hold day "
				"mode. Turn lightMonitor off to have the switching done here "
				"from the same thresholds, which also keeps the Preview "
				"buttons working.");
			warned_monitor = 1;
		}

		if (automate && !cfg.light_monitor && !cfg.have_sensor_pin) {
			if (cfg.min_threshold < 0 || cfg.max_threshold < 0) {
				if (!no_thresholds) {
					syslog(LOG_WARNING,
						"no automatic day/night: set nightMode.minThreshold "
						"and nightMode.maxThreshold in the web UI. Setting "
						"the pair is what enables it here - leave "
						"nightMode.lightMonitor off.");
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
				/* monitorDelay is how long the light has to STAY past a
				 * threshold before it is believed - not how long to wait after
				 * switching. The difference is the whole reason it is set: a
				 * torch swept across the lens must not change the mode, and a
				 * cooldown would not stop that, it would switch immediately and
				 * only then wait. Taken exactly as configured; second-guessing
				 * a number somebody chose in the web UI is not this program's
				 * job, and a default applies only when it is not set at all. */
				int delay = cfg.monitor_delay >= 0 ?
					cfg.monitor_delay : DEFAULT_DELAY;
				time_t now = time(NULL);

				/* No reading, no decision - but say so, once. This was silent
				 * before, and a silent do-nothing is indistinguishable from a
				 * camera that has simply decided it is still daytime. The
				 * usual cause is the source not being there: /proc/jz/isp
				 * appears with the ISP, so a stream that is not running has no
				 * AeAGain to read. */
				if (gain < 0) {
					if (!no_reading) {
						syslog(LOG_WARNING,
							"no light reading from the '%s' source, so "
							"nothing will switch%s", source,
							cfg.adc_readout ? " - is the photoresistor "
							"fitted, and isp.adcReadout right for this "
							"camera?" : " - is the stream running?");
						no_reading = 1;
					}
				} else if (no_reading) {
					syslog(LOG_INFO, "light reading from '%s' is back: %d",
						source, gain);
					no_reading = 0;
				}

				if (gain >= 0) {
					int want = night;
					if (gain > cfg.max_threshold) {
						want = 1;
					} else if (gain < cfg.min_threshold) {
						want = 0;
					}

					/* A hold is only ever protecting a choice that disagrees
					 * with the light. The moment the two agree there is
					 * nothing left to protect, so it is dropped rather than
					 * run down - otherwise switching to night by hand on a
					 * dark evening, which is exactly what the light wants
					 * anyway, would stop automation for five minutes for no
					 * reason. Checked every tick rather than only when the
					 * button is pressed, so it also covers the light coming
					 * round to agree with a choice made earlier. */
					if (want == night && hold_until) {
						hold_until = 0;
					}

					/* Restart the clock whenever the answer changes, so only an
					 * unbroken run past the threshold counts. */
					if (want != pending) {
						pending = want;
						pending_since = now;
					}

					if (want != night && now >= hold_until &&
					    now - pending_since >= delay &&
					    !request_night(want)) {
						syslog(LOG_INFO, "%s %d for %lds -> %s", source, gain,
							(long)(now - pending_since),
							want ? "night" : "day");
						requested = want;
						night = want;
					}
				}
			}
		}

		/* colorToGray is majestic's own setting for whether night mode should
		 * desaturate at all. Somebody who turns it off wants the IR-cut and the
		 * lamp without losing colour, and honouring it here is the difference
		 * between following that setting and ignoring it. */
		int grey = night && cfg.color_to_gray;

		if (grey != applied) {
			char reply[512];
			if (set_blackwhite(grey, reply, sizeof(reply))) {
				if (!complained) {
					syslog(LOG_WARNING,
						"cannot switch the ISP: no plugin on port %d "
						"and no tx_isp daynight parameter either",
						PLUGIN_PORT);
					complained = 1;
				}
			} else {
				syslog(LOG_INFO, "night %d grey %d: %s", night, grey, reply);
				applied = grey;
				complained = 0;
			}
		}

		usleep(POLL_MS * 1000);
	}

	return 0;
}
