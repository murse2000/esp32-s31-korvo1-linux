KORVO_UI_VERSION = 1.0
KORVO_UI_SITE = $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ui/src
KORVO_UI_SITE_METHOD = local
KORVO_UI_DEPENDENCIES = lvgl korvo-ppa
KORVO_UI_LICENSE = MIT
define KORVO_UI_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -ffunction-sections -fdata-sections -I$(STAGING_DIR)/usr/include/lvgl -I$(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-camera/src -I$(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src -c $(@D)/korvo_ui.c -o $(@D)/korvo_ui.o
	$(TARGET_CC) $(TARGET_CFLAGS) -ffunction-sections -fdata-sections -I$(STAGING_DIR)/usr/include/lvgl -c $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src/korvo_ppa_lvgl_fast.c -o $(@D)/korvo_ppa_lvgl.o
	$(TARGET_CXX) $(TARGET_LDFLAGS) -Wl,--gc-sections $(@D)/korvo_ui.o $(@D)/korvo_ppa_lvgl.o -L$(STAGING_DIR)/usr/lib -Wl,--start-group -llvgl_demos -llvgl -llvgl_thorvg -Wl,--end-group -lm -lpthread -o $(@D)/korvo-ui
	$(TARGET_CC) $(TARGET_CFLAGS) -ffunction-sections -fdata-sections -I$(STAGING_DIR)/usr/include/lvgl -c $(BR2_EXTERNAL_ESP32S31_PATH)/package/korvo-ppa/src/korvo_ppa_lvgl.c -o $(@D)/korvo_ppa_extra.o
	$(TARGET_CXX) $(TARGET_LDFLAGS) -Wl,--gc-sections $(@D)/korvo_ui.o $(@D)/korvo_ppa_extra.o -L$(STAGING_DIR)/usr/lib -Wl,--start-group -llvgl_demos -llvgl -llvgl_thorvg -Wl,--end-group -lm -lpthread -o $(@D)/korvo-ui-extra
endef
define KORVO_UI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/korvo-ui $(TARGET_DIR)/usr/bin/korvo-ui
	$(INSTALL) -D -m 0755 $(@D)/korvo-ui-extra $(TARGET_DIR)/usr/bin/korvo-ui-extra
endef
$(eval $(generic-package))
