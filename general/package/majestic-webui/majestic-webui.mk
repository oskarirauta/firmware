################################################################################
#
# majestic-webui
#
################################################################################

# Pre-minified distribution built by majestic-webui CI (.github/workflows/dist.yml) and
# published as a rolling release asset. Fetching the prepared artifact keeps this build
# hermetic (no JS/CSS toolchain) and the webui source in git unminified. `dist` is a
# moving ref, so the "Refresh moving-ref package downloads" CI step keeps it from
# going stale in the dl cache.
MAJESTIC_WEBUI_VERSION = dist
MAJESTIC_WEBUI_SITE = https://github.com/openipc/majestic-webui/releases/download/dist
MAJESTIC_WEBUI_SOURCE = majestic-webui-dist.tar.gz
MAJESTIC_WEBUI_LICENSE = MIT
MAJESTIC_WEBUI_LICENSE_FILES = LICENSE

ifeq ($(OPENIPC_VARIANT),fpv)
	VERSION = FPV
else
	VERSION = STANDARD
endif

define MAJESTIC_WEBUI_INSTALL
	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr
	cp -r $(@D)/sbin $(TARGET_DIR)/usr
	[ -d $(@D)/bin ] && cp -r $(@D)/bin $(TARGET_DIR)/usr || true

	$(INSTALL) -m 755 -d $(TARGET_DIR)/var
	cp -r $(@D)/www $(TARGET_DIR)/var
endef

define MAJESTIC_WEBUI_STANDARD_FIXUP
	rm $(TARGET_DIR)/var/www/cgi-bin/fpv-wfb.cgi
	rm $(TARGET_DIR)/var/www/cgi-bin/j/locale_fpv.cgi
	rm $(TARGET_DIR)/var/www/cgi-bin/p/header_fpv.cgi
	rm $(TARGET_DIR)/var/www/cgi-bin/p/fpv_common.cgi
endef

define MAJESTIC_WEBUI_FPV_FIXUP
	mv -f $(TARGET_DIR)/var/www/cgi-bin/j/locale_fpv.cgi $(TARGET_DIR)/var/www/cgi-bin/j/locale.cgi
	mv -f $(TARGET_DIR)/var/www/cgi-bin/p/header_fpv.cgi $(TARGET_DIR)/var/www/cgi-bin/p/header.cgi
	rm $(TARGET_DIR)/usr/sbin/telegram
	rm $(TARGET_DIR)/usr/sbin/openwall
endef

# The settings page grows a live preview panel only when a tab holds at least
# one property the schema marks "x-live". This majestic emits that flag nowhere
# at all - `strings` finds no occurrence of it in the binary - so the panel can
# never appear, and the image knobs have to be judged by switching to the
# preview page and back. Saving them takes effect within a second or two on this
# board, so seeing the result in place is worth having.
#
# Done as an edit rather than a patch because the file is minified onto one
# line: a one-token change diffs as forty kilobytes. The anchor occurs exactly
# once, and the result is verified so a future dist that moves it fails the
# build here rather than silently dropping the panel.
#
# The anchor deliberately excludes the leading "return": sed's & is the whole
# match, and including it produced "return X||return Y" - a syntax error that
# took the whole script out and left the settings page stuck on "Loading
# settings". The check below looks at the finished expression for that reason,
# not merely for the inserted text.
define MAJESTIC_WEBUI_SETTINGS_PREVIEW
	$(SED) 's#e.sections.some((e=>{const n=#e.sections.includes("image")||&#' \
		$(TARGET_DIR)/var/www/a/mj-settings.js
	grep -q 'return e.sections.includes("image")||e.sections.some' \
		$(TARGET_DIR)/var/www/a/mj-settings.js
endef

# Live image tuning, where a plugin is installed to do it. The streamer applies
# image settings on save, so choosing one means saving, looking and saving
# again; the plugin can set the running ISP directly, so the picture can follow
# the slider instead.
#
# The script tag goes into mj-settings.cgi, which is plain text, rather than
# into mj-settings.js, which ships minified - a change there cannot be reviewed
# in a diff. The script itself binds nothing at load time and listens for input
# events on the document, so it needs to know nothing about when the settings
# form is built or rebuilt. The result is checked, so a future dist that moves
# the anchor fails the build here instead of silently shipping a dead file.
define MAJESTIC_WEBUI_PLUGIN_LIVE
	$(INSTALL) -m 755 -D $(MAJESTIC_WEBUI_PKGDIR)/files/plugin.cgi \
		$(TARGET_DIR)/var/www/cgi-bin/j/plugin.cgi
	$(INSTALL) -m 644 -D $(MAJESTIC_WEBUI_PKGDIR)/files/plugin-live.js \
		$(TARGET_DIR)/var/www/a/plugin-live.js
	$(SED) 's|<script src="/a/mj-settings.js" defer></script>|&\n<script src="/a/plugin-live.js" defer></script>|' \
		$(TARGET_DIR)/var/www/cgi-bin/mj-settings.cgi
	grep -q 'plugin-live.js' $(TARGET_DIR)/var/www/cgi-bin/mj-settings.cgi
endef

define MAJESTIC_WEBUI_INSTALL_TARGET_CMDS
	$(MAJESTIC_WEBUI_INSTALL)
	$(MAJESTIC_WEBUI_$(VERSION)_FIXUP)
	$(MAJESTIC_WEBUI_SETTINGS_PREVIEW)
	$(if $(BR2_PACKAGE_MAJESTIC_PLUGINS),$(MAJESTIC_WEBUI_PLUGIN_LIVE))
endef

$(eval $(generic-package))
