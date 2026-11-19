KORVO_CAMERA_VERSION = 1.0
KORVO_CAMERA_SITE = $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-camera/src
KORVO_CAMERA_SITE_METHOD = local
KORVO_CAMERA_LICENSE = GPL-2.0-only AND Apache-2.0
define KORVO_CAMERA_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) $(@D)/camera_capture.c -o $(@D)/korvo-camera
endef
define KORVO_CAMERA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/korvo-camera $(TARGET_DIR)/usr/bin/korvo-camera
endef
$(eval $(kernel-module))
$(eval $(generic-package))
