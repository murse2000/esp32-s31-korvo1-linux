################################################################################
# LVGL Korvo-1 music demo
################################################################################

LVGL_MUSIC_DEMO_VERSION = 1.0
LVGL_MUSIC_DEMO_SITE = $(BR2_EXTERNAL_ESP32S31_PATH)/package/lvgl-music-demo/src
LVGL_MUSIC_DEMO_SITE_METHOD = local
LVGL_MUSIC_DEMO_DEPENDENCIES = lvgl alsa-utils korvo-ppa

# Keep demo files out of the NOR partitions; this package is selected by the
# Korvo-1 disk-root profile and installs onto the microSD ext4 root.
define LVGL_MUSIC_DEMO_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-camera/src \
		-I$(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src \
		-I$(STAGING_DIR)/usr/include/lvgl \
		-I$(STAGING_DIR)/usr/include/lvgl/src/drivers/display/fb \
		-I$(STAGING_DIR)/usr/include/lvgl/src/drivers/evdev \
		-c $(@D)/lvgl_music_demo.c -o $(@D)/lvgl_music_demo.o
	$(TARGET_CC) $(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/lvgl -c $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src/korvo_ppa_lvgl.c -o $(@D)/korvo_ppa_lvgl.o
	$(TARGET_CXX) $(TARGET_LDFLAGS) $(@D)/lvgl_music_demo.o $(@D)/korvo_ppa_lvgl.o -o $(@D)/lvgl-music-demo \
		-L$(STAGING_DIR)/usr/lib -Wl,--start-group -llvgl -llvgl_thorvg -Wl,--end-group -lm -lpthread
endef

define LVGL_MUSIC_DEMO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/lvgl-music-demo $(TARGET_DIR)/usr/bin/lvgl-music-demo
endef

$(eval $(generic-package))
