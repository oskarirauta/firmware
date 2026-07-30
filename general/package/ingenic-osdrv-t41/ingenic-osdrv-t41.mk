################################################################################
#
# ingenic-osdrv-t41
#
################################################################################

INGENIC_OSDRV_T41_VERSION =
INGENIC_OSDRV_T41_SITE =
INGENIC_OSDRV_T41_LICENSE = MIT
INGENIC_OSDRV_T41_LICENSE_FILES = LICENSE

# The kernel side - tx-isp, avpu, audio, the sensor driver and friends - is
# built from source by the ingenic-opensdk package. This package carries only
# the userspace MPP libraries, the sensor description and ISP tuning data, and
# the module loader.
INGENIC_OSDRV_T41_DEPENDENCIES = host-patchelf uclibc-compat

# The vendor libraries are uClibc builds and name libc.so.0, libpthread.so.0
# and libdl.so.0 in their NEEDED entries. musl provides none of those SONAMEs
# and folds pthread and dl into libc.
#
# libpthread and libdl simply go away. libc.so.0 is *replaced* by
# libuclibc-compat.so rather than dropped, because libimp.so calls one uClibc
# internal that musl does not export - __fputc_unlocked - and that shim already
# provides it (OpenIPC ships it for the hi3516cv100 vendor blobs). Everything
# else resolves against the musl already mapped into the process.
#
# The alternative was ingenic-lib's glibc build of the same release, which does
# not use __fputc_unlocked. It was rejected: it needs __pthread_register_cancel,
# __pthread_unregister_cancel and __pthread_unwind_next, glibc's unwind-based
# cancellation internals, which have no musl equivalent. One trivial shim beats
# three hard ones.
#
# See PROVENANCE.md for the upstream origin and checksums.
define INGENIC_OSDRV_T41_UNNEED_UCLIBC
	for lib in libimp.so libalog.so libsysutils.so; do \
		$(HOST_DIR)/bin/patchelf \
			--replace-needed libc.so.0 libuclibc-compat.so \
			--remove-needed libpthread.so.0 \
			--remove-needed libdl.so.0 \
			$(TARGET_DIR)/usr/lib/$$lib || exit 1; \
	done
endef

# OpenIPC builds majestic per SOC_FAMILY and T41 rides the t40 family, so the
# binary is majestic.t40 while the libraries under it are T41's. The two
# disagree about IMPEncoderChnAttr and IMP_Encoder_CreateChn() rejects the
# block majestic hands it. See files/src/imp_t41_shim.c for the detail;
# S95majestic preloads the result when this file is present.
define INGENIC_OSDRV_T41_BUILD_CMDS
	mkdir -p $(@D)
	$(TARGET_CC) $(TARGET_CFLAGS) -fPIC -shared -Wall \
		-o $(@D)/libimp_t41_shim.so \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/src/imp_t41_shim.c \
		$(TARGET_LDFLAGS)
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall \
		-o $(@D)/nightsync \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/src/nightsync.c \
		$(TARGET_LDFLAGS)
endef

define INGENIC_OSDRV_T41_INSTALL_TARGET_CMDS
	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc/sensor
	$(INSTALL) -m 644 -t $(TARGET_DIR)/etc/sensor \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/sensor/$(OPENIPC_SNS_MODEL).yaml
	$(INSTALL) -m 644 -t $(TARGET_DIR)/etc/sensor \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/sensor/params/$(OPENIPC_SNS_MODEL)-$(OPENIPC_SOC_MODEL).bin

	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 755 -t $(TARGET_DIR)/usr/bin \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/script/load_ingenic
	$(INSTALL) -m 755 -t $(TARGET_DIR)/usr/bin $(@D)/nightsync

	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc/init.d
	$(INSTALL) -m 755 -t $(TARGET_DIR)/etc/init.d \
		$(INGENIC_OSDRV_T41_PKGDIR)/files/script/S96nightsync
# Bake in the sensor this image was built for. It is the last resort in
# load_ingenic, used when the u-boot environment names no sensor and sinfo
# cannot identify one - which is what happens on an untouched camera, because
# sinfo's table has no gc5603. The image ships exactly one sensor driver and one
# ISP tuning file, so this is the only sensor it could have driven anyway.
	$(SED) 's|@SNS_MODEL@|$(OPENIPC_SNS_MODEL)|' $(TARGET_DIR)/usr/bin/load_ingenic

	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc/default
	$(INSTALL) -m 644 $(INGENIC_OSDRV_T41_PKGDIR)/files/script/majestic-env \
		$(TARGET_DIR)/etc/default/majestic



	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/lib
	$(INSTALL) -m 644 -t $(TARGET_DIR)/usr/lib $(INGENIC_OSDRV_T41_PKGDIR)/files/lib/*.so
	$(INGENIC_OSDRV_T41_UNNEED_UCLIBC)

	$(INSTALL) -m 644 -t $(TARGET_DIR)/usr/lib $(@D)/libimp_t41_shim.so
endef


# majestic loads plugins only when it is told to, and the plugin is where the
# grey conversion lives on this SoC - so without this the Preview page's Night
# button moves the filter and leaves the picture in colour. Done as a finalize
# hook rather than in the install step because majestic writes this file itself
# and the two packages have no ordering between them; finalize runs after both.
ifeq ($(BR2_PACKAGE_MAJESTIC_PLUGINS),y)
define INGENIC_OSDRV_T41_ENABLE_PLUGINS
	if ! grep -q '^  plugins:' $(TARGET_DIR)/etc/majestic.yaml; then \
		$(SED) '/^system:/a\  plugins: true' $(TARGET_DIR)/etc/majestic.yaml; \
	fi
	grep -q '^  plugins: true' $(TARGET_DIR)/etc/majestic.yaml
endef
ifeq ($(BR2_PACKAGE_INGENIC_OSDRV_T41),y)
TARGET_FINALIZE_HOOKS += INGENIC_OSDRV_T41_ENABLE_PLUGINS
endif
endif

$(eval $(generic-package))
