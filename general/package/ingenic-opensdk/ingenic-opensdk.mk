################################################################################
#
# ingenic-opensdk
#
################################################################################

INGENIC_OPENSDK_SITE = $(call github,openipc,openingenic,$(INGENIC_OPENSDK_VERSION))
INGENIC_OPENSDK_VERSION = HEAD

INGENIC_OPENSDK_LICENSE = GPL-3.0
INGENIC_OPENSDK_LICENSE_FILES = LICENSE

INGENIC_OPENSDK_MODULE_SUBDIRS = kernel
INGENIC_OPENSDK_MODULE_MAKE_OPTS = \
	SOC=$(OPENIPC_SOC_MODEL) \
	SNS=$(OPENIPC_SNS_MODEL) \
	INSTALL_MOD_PATH=$(TARGET_DIR) \
	INSTALL_MOD_DIR=ingenic

# The T41 ISP firmware blobs carry a vendor-private bit in their MIPS e_flags
# that binutils 2.40 refuses to merge. See clear-vendor-eflags.py for the full
# explanation; it is a no-op for every other SoC.
define INGENIC_OPENSDK_CLEAR_VENDOR_EFLAGS
	$(Q)python3 $(INGENIC_OPENSDK_PKGDIR)/clear-vendor-eflags.py \
		$(wildcard $(@D)/kernel/isp/t41/libt41-firmware-*.a)
endef
INGENIC_OPENSDK_POST_PATCH_HOOKS += INGENIC_OPENSDK_CLEAR_VENDOR_EFLAGS

$(eval $(kernel-module))
$(eval $(generic-package))
