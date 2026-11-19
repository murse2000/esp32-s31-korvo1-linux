KORVO_PPA_VERSION = 1.0
KORVO_PPA_SITE = $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src
KORVO_PPA_SITE_METHOD = local
KORVO_PPA_LICENSE = GPL-2.0-only AND MIT

define KORVO_PPA_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) $(@D)/ppa_test.c -o $(@D)/ppa-test
endef
define KORVO_PPA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ppa-test $(TARGET_DIR)/usr/bin/ppa-test
endef
$(eval $(kernel-module))
$(eval $(generic-package))
