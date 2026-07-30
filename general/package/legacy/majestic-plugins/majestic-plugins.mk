################################################################################
#
# majestic-plugins
#
################################################################################

MAJESTIC_PLUGINS_SITE_METHOD = git
MAJESTIC_PLUGINS_SITE = https://github.com/openipc/majestic-plugins
MAJESTIC_PLUGINS_VERSION = HEAD

# Relicensed on 2025-11-21: free for noncommercial use, thirty days for
# commercial. Declared so that a build reports it, and worth knowing before
# enabling this in any board's default configuration.
MAJESTIC_PLUGINS_LICENSE = Prosperity-3.0.0
MAJESTIC_PLUGINS_LICENSE_FILES = LICENSE.md

MAJESTIC_PLUGINS_GIT_SUBMODULES = YES

define MAJESTIC_PLUGINS_BUILD_CMDS
	$(MAKE) CC=$(TARGET_CC) TARGET=$(OPENIPC_SOC_VENDOR) -C $(@D) -B
endef

define MAJESTIC_PLUGINS_INSTALL_TARGET_CMDS
	$(INSTALL) -m 644 -t $(TARGET_DIR)/usr/lib $(@D)/$(OPENIPC_SOC_VENDOR).so
endef

$(eval $(generic-package))
