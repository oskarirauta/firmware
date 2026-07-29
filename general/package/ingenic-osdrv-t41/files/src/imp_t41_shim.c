// SPDX-License-Identifier: MIT
/*
 * imp_t41_shim - let the T40 build of majestic drive T41's encoder.
 *
 * OpenIPC builds majestic per SOC_FAMILY and T41 rides the t40 family, so the
 * binary we install is majestic.t40 while the MPP libraries under it are
 * T41's. Two of the encoder structures grew between those generations, and
 * because majestic sizes its buffers from the T40 headers, T41's libimp writes
 * past the end of them.
 *
 * Both extents were measured by disassembling the vendor libraries and reading
 * off the store offsets each version makes into the caller's buffer:
 *
 *   IMP_Encoder_SetDefaultParam   T40 writes ..108  (112 bytes)
 *                                 T41 writes ..116  (120 bytes)   8 bytes over
 *   IMP_Encoder_GetStream         T40 writes ..20   (24 bytes)
 *                                 T41 writes ..24   (28 bytes)    4 bytes over
 *
 * The GetStream overrun happens on every frame, so it corrupts whatever
 * happens to sit after majestic's IMPEncoderStream. That is why the crash used
 * to land in a different place on every run - once in libevent, once a silent
 * SIGSEGV, once a hang - rather than anywhere near libimp.
 *
 * The fix is the same in both cases: let libimp write into a buffer of ours
 * that is large enough, and hand back only as many bytes as the T40 build
 * expects to own.
 *
 * IMP_Encoder_CreateChn needs different treatment. T41's libimp validates a
 * final-encode colour-space field that T40's does not have at all
 *
 *   invalid final encode color space:%d (only support NV12 or T420)
 *
 * and rejects the block majestic assembled at T40 offsets, before the Allegro
 * core is ever reached - which is why that failure carried no AL_* error. We
 * do not need to know the layout to fix it: majestic configures the encoder
 * solely through IMP_Encoder_SetDefaultParam, importing no other encoder
 * setter, so everything it wants is carried in that call's arguments. We
 * remember them and let libimp build a native T41 block from them at
 * CreateChn time. Entries are keyed by the attribute pointer because
 * SetDefaultParam takes no channel number; a block we never saw configured is
 * passed through untouched.
 *
 * IMP_Encoder_ReleaseStream needs no help: both versions read only offset 12
 * of the caller's stream, the pack pointer.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* Channels are validated as chn < 8 inside libimp. */
#define MAX_CHN		8
#define MAX_TRACKED	8

/* What each generation writes into the caller's buffer. */
#define ATTR_T40_BYTES		112
#define STREAM_T40_BYTES	24

/* Our own scratch, comfortably larger than either generation needs. */
#define ATTR_SCRATCH	512
#define STREAM_SCRATCH	128

struct recorded {
	const void *key;
	int profile, rc_mode, width, height;
	int fps_num, fps_den, gop_len, same_scene;
	int quality, bitrate;
	unsigned char attr[ATTR_SCRATCH] __attribute__((aligned(16)));
};

static struct recorded tracked[MAX_TRACKED];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* One per channel: streams are pulled by per-channel threads. */
static unsigned char stream_scratch[MAX_CHN][STREAM_SCRATCH]
	__attribute__((aligned(16)));

static int (*real_set_default)(void *, int, int, int, int,
			       int, int, int, int, int, int);
static int (*real_create_chn)(int, void *);
static int (*real_get_stream)(int, void *, int);
static int (*real_start_recv)(int);

/*
 * Applied from the frame path, so it needs declaring before the encoder hooks;
 * the definition is further down with the rest of the image handling.
 */
static void maybe_apply_image_knobs(void);

static void resolve(void)
{
	if (!real_set_default)
		real_set_default = dlsym(RTLD_NEXT, "IMP_Encoder_SetDefaultParam");
	if (!real_create_chn)
		real_create_chn = dlsym(RTLD_NEXT, "IMP_Encoder_CreateChn");
	if (!real_get_stream)
		real_get_stream = dlsym(RTLD_NEXT, "IMP_Encoder_GetStream");
	if (!real_start_recv)
		real_start_recv = dlsym(RTLD_NEXT, "IMP_Encoder_StartRecvPic");
}

/* Caller holds the lock. */
static struct recorded *slot_for(const void *key)
{
	int i, free_slot = -1;

	for (i = 0; i < MAX_TRACKED; i++) {
		if (tracked[i].key == key)
			return &tracked[i];
		if (!tracked[i].key && free_slot < 0)
			free_slot = i;
	}
	return free_slot < 0 ? NULL : &tracked[free_slot];
}

int IMP_Encoder_SetDefaultParam(void *attr, int profile, int rc_mode,
				int width, int height, int fps_num, int fps_den,
				int gop_len, int same_scene, int quality,
				int bitrate)
{
	struct recorded *r;
	int ret;

	resolve();
	if (!real_set_default)
		return -1;

	pthread_mutex_lock(&lock);
	r = slot_for(attr);
	if (!r) {
		pthread_mutex_unlock(&lock);
		return real_set_default(attr, profile, rc_mode, width, height,
					fps_num, fps_den, gop_len, same_scene,
					quality, bitrate);
	}

	r->key        = attr;
	r->profile    = profile;
	r->rc_mode    = rc_mode;
	r->width      = width;
	r->height     = height;
	r->fps_num    = fps_num;
	r->fps_den    = fps_den;
	r->gop_len    = gop_len;
	r->same_scene = same_scene;
	r->quality    = quality;
	r->bitrate    = bitrate;

	/*
	 * Fill our block, not majestic's - T41 writes eight bytes further than
	 * the T40 build reserved. majestic still gets an initialised buffer,
	 * clipped to what it owns; CreateChn ignores it and uses ours.
	 */
	memset(r->attr, 0, sizeof(r->attr));
	ret = real_set_default(r->attr, profile, rc_mode, width, height,
			       fps_num, fps_den, gop_len, same_scene,
			       quality, bitrate);
	if (!ret)
		memcpy(attr, r->attr, ATTR_T40_BYTES);
	pthread_mutex_unlock(&lock);
	return ret;
}

int IMP_Encoder_CreateChn(int chn, void *attr)
{
	static int announced;
	struct recorded *r = NULL;
	int i, ret;

	resolve();
	if (!real_create_chn)
		return -1;

	pthread_mutex_lock(&lock);
	for (i = 0; i < MAX_TRACKED; i++) {
		if (tracked[i].key == attr) {
			r = &tracked[i];
			break;
		}
	}
	if (!r) {
		pthread_mutex_unlock(&lock);
		return real_create_chn(chn, attr);
	}

	/*
	 * Rebuild rather than patch: the fields majestic wrote itself sit at
	 * T40 offsets and would land in the wrong places here.
	 */
	memset(r->attr, 0, sizeof(r->attr));
	ret = real_set_default(r->attr, r->profile, r->rc_mode, r->width,
			       r->height, r->fps_num, r->fps_den, r->gop_len,
			       r->same_scene, r->quality, r->bitrate);
	if (ret) {
		pthread_mutex_unlock(&lock);
		return real_create_chn(chn, attr);
	}

	if (!announced) {
		announced = 1;
		fprintf(stderr, "imp_t41_shim: encoder attributes rebuilt for "
				"the T41 libimp\n");
	}

	ret = real_create_chn(chn, r->attr);
	pthread_mutex_unlock(&lock);
	return ret;
}

int IMP_Encoder_GetStream(int chn, void *stream, int block)
{
	unsigned char *buf;
	int ret;

	resolve();
	if (!real_get_stream)
		return -1;

	if (chn < 0 || chn >= MAX_CHN || !stream)
		return real_get_stream(chn, stream, block);

	/*
	 * The hot path: T41 writes 28 bytes here and the T40 build reserved
	 * 24. Unfixed, this smears four bytes over whatever follows the
	 * caller's IMPEncoderStream once per frame.
	 */
	buf = stream_scratch[chn];
	memset(buf, 0, STREAM_SCRATCH);
	ret = real_get_stream(chn, buf, block);
	if (!ret) {
		memcpy(stream, buf, STREAM_T40_BYTES);
		maybe_apply_image_knobs();
	}
	return ret;
}

/*
 * majestic scales these 0..100 - read off the schema it serves at
 * /api/v1/config.schema.json - while IMP takes 0..255. A value outside the
 * range is dropped rather than clamped, so a key we misread cannot quietly
 * wash the picture out.
 */
#define MAJESTIC_YAML	"/etc/majestic.yaml"

static int scale_to_imp(int v)
{
	if (v < 0 || v > 100)
		return -1;
	return (v * 255 + 50) / 100;
}

/*
 * A deliberately small reader rather than a YAML parser to maintain:
 * majestic.yaml is flat, so all we need is to know when we are inside the
 * "image:" block and to pick three scalars out of it.
 */
static void read_image_knobs(int *brightness, int *contrast, int *saturation,
			     int *mirror, int *flip)
{
	char line[256];
	int in_image = 0;
	FILE *f = fopen(MAJESTIC_YAML, "r");

	if (!f)
		return;

	while (fgets(line, sizeof(line), f)) {
		char *key = line, *colon, *val;
		int value;

		if (*line == '\n' || *line == '\r' || *line == '#')
			continue;

		/* A top-level key starts in column zero. */
		if (*line != ' ' && *line != '\t') {
			in_image = !strncmp(line, "image:", 6);
			continue;
		}
		if (!in_image)
			continue;

		while (*key == ' ' || *key == '\t')
			key++;
		if (*key == '#')
			continue;

		colon = strchr(key, ':');
		if (!colon)
			continue;
		*colon = '\0';
		val = colon + 1;
		while (*val == ' ' || *val == '\t')
			val++;
		value = (int)strtol(val, NULL, 10);

		if (!strcmp(key, "luminance"))
			*brightness = scale_to_imp(value);
		else if (!strcmp(key, "contrast"))
			*contrast = scale_to_imp(value);
		else if (!strcmp(key, "saturation"))
			*saturation = scale_to_imp(value);
		else if (!strcmp(key, "mirror"))
			*mirror = !strncmp(val, "true", 4);
		else if (!strcmp(key, "flip"))
			*flip = !strncmp(val, "true", 4);
	}
	fclose(f);
}

#define ISP_PARAMS	"/sys/module/tx_isp_t41/parameters/"

/*
 * The knobs are set through the ISP driver, not through libimp.
 *
 * libimp's own route is broken on T41 and no ordering fixes it:
 * IMP_ISP_Tuning_Set* answers -4084 for every value, before StartRecvPic,
 * after it, and with frames demonstrably flowing. Disassembly puts the failure
 * in get_isptuningdev(), which hands back a NULL device even though an open()
 * trace shows the library holding /dev/isp-m0 on a live descriptor - the
 * handle never reaches the tuning context. IMP_ISP_Tuning_SetISPRunningMode is
 * built the same way, which is why majestic's own day/night switching is
 * equally dead here.
 *
 * The firmware's setters underneath work perfectly: tisp_set_brightness and
 * friends are global symbols in tx-isp-t41.ko, and the driver patch that
 * exposes them as module parameters makes them reachable. Writing there takes
 * effect on the live stream, immediately.
 *
 * Returns 1 if the value was written, 0 if it was already right or not
 * configured, and -1 if the write failed.
 */
static int write_param(const char *path, int value)
{
	char buf[16];
	FILE *f;
	int now = -1;

	if (value < 0)
		return 0;

	/* The parameter reads back, so an unchanged value costs no write. */
	f = fopen(path, "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f))
			now = (int)strtol(buf, NULL, 10);
		fclose(f);
	}
	if (now == value)
		return 0;

	f = fopen(path, "w");
	if (!f)
		return -1;
	fprintf(f, "%d\n", value);
	return fclose(f) ? -1 : 1;
}

static int write_knob(const char *name, int value)
{
	char path[sizeof(ISP_PARAMS) + 16];

	snprintf(path, sizeof(path), ISP_PARAMS "%s", name);
	return write_param(path, value);
}

/*
 * Image orientation is the sensor's job on T41, not the ISP's: libimp exports
 * no working flip, and majestic references no flip symbol at all, so its mirror
 * and flip settings have nowhere to go. The sensor driver takes shvflip as a
 * writable module parameter and re-applies it at every stream-on - and saving
 * from the web UI sends majestic a SIGHUP, which takes the SDK down and back
 * up, so a value written here lands on the same Save that applies the colours.
 *
 * The module is found rather than named: this package serves T41 generally, and
 * the sensor differs per camera.
 */
static const char *sensor_flip_path(void)
{
	static char path[128];
	static int looked;
	struct dirent *e;
	DIR *d;

	if (looked)
		return path[0] ? path : NULL;
	looked = 1;

	d = opendir("/sys/module");
	if (!d)
		return NULL;

	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "sensor_", 7))
			continue;
		snprintf(path, sizeof(path),
			 "/sys/module/%s/parameters/shvflip", e->d_name);
		if (!access(path, W_OK))
			break;
		path[0] = '\0';
	}
	closedir(d);
	return path[0] ? path : NULL;
}

static void apply_image_knobs(void)
{
	int brightness = -1, contrast = -1, saturation = -1;
	int mirror = 0, flip = 0;
	const char *orientation;
	int wb, wc, ws, wo = 0;

	read_image_knobs(&brightness, &contrast, &saturation, &mirror, &flip);

	wb = write_knob("brightness", brightness);
	wc = write_knob("contrast", contrast);
	ws = write_knob("saturation", saturation);

	/* 0 none, 1 mirror, 2 flip, 3 both - the driver's own numbering. */
	orientation = sensor_flip_path();
	if (orientation)
		wo = write_param(orientation, (mirror ? 1 : 0) | (flip ? 2 : 0));

	if (wo > 0)
		fprintf(stderr, "imp_t41_shim: sensor orientation set to %d "
				"(mirror %d, flip %d)\n",
				(mirror ? 1 : 0) | (flip ? 2 : 0), mirror, flip);

	if (wb > 0 || wc > 0 || ws > 0)
		fprintf(stderr, "imp_t41_shim: image knobs applied to the ISP "
				"(luminance %d, contrast %d, saturation %d "
				"of 255)\n", brightness, contrast, saturation);
	else if (wb < 0 || wc < 0 || ws < 0 || wo < 0)
		fprintf(stderr, "imp_t41_shim: cannot write the ISP knobs under "
				ISP_PARAMS " (%s)\n", strerror(errno));
}

/*
 * Re-read the configuration when it changes, so the web UI's sliders take
 * effect while the stream runs instead of at the next restart - which is how
 * they behave on the platforms majestic drives itself.
 *
 * Driven from the frame path because a delivered frame proves the ISP is up,
 * and the driver's setters index a per-instance table that only exists then.
 * Every sixteenth frame is about a second at these rates, and a stat() at that
 * rate costs nothing next to encoding one.
 */
static void maybe_apply_image_knobs(void)
{
	static unsigned long frames;
	static time_t applied;
	struct stat st;

	if (++frames & 15)
		return;
	if (stat(MAJESTIC_YAML, &st))
		return;
	if (st.st_mtime == applied)
		return;

	applied = st.st_mtime;
	apply_image_knobs();
}

/*
 * Hooked here rather than at CreateChn: by the time majestic starts receiving
 * pictures the ISP is fully up and tuning is enabled, whereas the encoder
 * channel exists well before that.
 *
 * Applied on every call, not once per process. Saving settings in the web UI
 * sends majestic a SIGHUP, and it reloads the configuration and brings the SDK
 * back up inside the same process - so the ISP returns to its defaults while a
 * once-only flag would never fire again, and the knobs silently stopped
 * working after the first reload. Re-reading a flat file and issuing three
 * setter calls at stream start costs nothing worth saving.
 */
int IMP_Encoder_StartRecvPic(int chn)
{
	resolve();
	if (!real_start_recv)
		return -1;

	return real_start_recv(chn);
}


/*
 * Refuse to join a thread that was never started.
 *
 * Saving settings from the web UI is a JSON POST to /api/v1/config followed by
 * SIGHUP. On Ingenic that pair kills majestic, and it reproduces from a shell:
 *
 *     wget -qO- --header='Content-Type: application/json' \
 *          --post-data='{"audio":{"enabled":true}}' \
 *          http://127.0.0.1/api/v1/config    # "Not implemented for platform
 *                                            #  ingenic"
 *     killall -HUP majestic                  # SIGSEGV
 *
 * The identical SIGHUP on its own is survived completely - the SDK stops, the
 * configuration is re-read, the sensor comes back and the stream resumes. So
 * the POST hits something unimplemented on this platform, leaves a thread
 * unstarted, and the reload afterwards tries to join it. musl trusts the
 * handle and reads straight through it, which is the fault at NULL+0x18 that
 * every one of those crash reports pointed at.
 *
 * A null handle is not a thread, so say so rather than dying: ESRCH is exactly
 * what pthread_join is specified to return for a thread that is not there.
 * Real joins are untouched, so this hides nothing else.
 */
static int (*real_pthread_join)(pthread_t, void **);

int pthread_join(pthread_t thread, void **retval)
{
	if (!real_pthread_join)
		real_pthread_join = dlsym(RTLD_NEXT, "pthread_join");
	if (!thread || !real_pthread_join)
		return ESRCH;

	return real_pthread_join(thread, retval);
}
