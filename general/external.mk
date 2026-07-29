export OPENIPC_SOC_VENDOR := $(call qstrip,$(BR2_OPENIPC_SOC_VENDOR))
export OPENIPC_SOC_MODEL := $(call qstrip,$(BR2_OPENIPC_SOC_MODEL))
export OPENIPC_SOC_ALIASES := $(call qstrip,$(BR2_OPENIPC_SOC_ALIASES))
export OPENIPC_SOC_FAMILY := $(call qstrip,$(BR2_OPENIPC_SOC_FAMILY))
export OPENIPC_SNS_MODEL := $(call qstrip,$(BR2_OPENIPC_SNS_MODEL))
export OPENIPC_VARIANT := $(call qstrip,$(BR2_OPENIPC_VARIANT))
export OPENIPC_MAJESTIC := $(call qstrip,$(BR2_OPENIPC_MAJESTIC))
export WGET := wget --show-progress --passive-ftp -nd -t5 -T10

EXTERNAL_VENDOR := $(BR2_EXTERNAL)/../br-ext-chip-$(OPENIPC_SOC_VENDOR)
OPENIPC_KERNEL := $(OPENIPC_SOC_VENDOR)-$(OPENIPC_SOC_FAMILY)
OPENIPC_TOOLCHAIN := toolchain/toolchain.$(OPENIPC_KERNEL)

include $(sort $(wildcard $(BR2_EXTERNAL)/package/*/*.mk))
include $(sort $(wildcard $(BR2_EXTERNAL)/package/legacy/*/*.mk))

# buildroot's own UBOOT_COPY_OLD_LICENSE_FILE post-extract hook exists for
# U-Boot older than 2013.10, which kept its licence text in COPYING rather than
# in Licenses/. It runs unconditionally, and on any modern U-Boot - where
# COPYING is a symlink to Licenses/gpl-2.0.txt - it aborts the extract step with
#
#   install: 'COPYING' and 'Licenses/gpl-2.0.txt' are the same file
#
# so no board can build a current U-Boot until it is dealt with. The hook is
# replaced rather than dropped, so the pre-2013.10 case it was written for still
# works; the only change is that it now skips the copy when source and
# destination are already the same file.
#
# This lives here because $(BR2_EXTERNAL_MKS) is included after boot/common.mk,
# and package recipes are expanded when they run rather than when they are
# defined - so reassigning the hook list at this point still takes effect.
define OPENIPC_UBOOT_COPY_OLD_LICENSE_FILE
	if [ -f $(@D)/COPYING ] && \
	   [ ! $(@D)/COPYING -ef $(@D)/Licenses/gpl-2.0.txt ]; then \
		$(INSTALL) -m 0644 -D $(@D)/COPYING $(@D)/Licenses/gpl-2.0.txt; \
	fi
endef

UBOOT_POST_EXTRACT_HOOKS := \
	$(filter-out UBOOT_COPY_OLD_LICENSE_FILE,$(UBOOT_POST_EXTRACT_HOOKS)) \
	OPENIPC_UBOOT_COPY_OLD_LICENSE_FILE
