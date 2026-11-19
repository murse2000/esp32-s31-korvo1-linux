################################################################################
# LVGL
################################################################################

LVGL_VERSION = 9.2.2
LVGL_SITE = $(call github,lvgl,lvgl,v$(LVGL_VERSION))
LVGL_INSTALL_STAGING = YES
LVGL_LICENSE = MIT
LVGL_LICENSE_FILES = LICENCE.txt
LVGL_CONF_OPTS = \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -ffunction-sections -fdata-sections" \
	-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -ffunction-sections -fdata-sections -include stdint.h" \
	-DLV_CONF_PATH=lv_conf.h \
	-DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
	-DLV_CONF_BUILD_DISABLE_DEMOS=OFF \
	-DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=OFF \
	-DBUILD_SHARED_LIBS=OFF

define LVGL_COPY_CONFIG
	$(INSTALL) -D -m 0644 $(LVGL_PKGDIR)/lv_conf.h $(@D)/src/lv_conf.h
	$(INSTALL) -D -m 0644 $(LVGL_PKGDIR)/lv_conf.h $(@D)/lv_conf.h
endef
LVGL_POST_EXTRACT_HOOKS += LVGL_COPY_CONFIG

define LVGL_INSTALL_LINUX_HEADERS
	$(INSTALL) -D -m 0644 $(@D)/src/drivers/display/fb/lv_linux_fbdev.h \
		$(STAGING_DIR)/usr/include/lvgl/src/drivers/display/fb/lv_linux_fbdev.h
	$(INSTALL) -D -m 0644 $(@D)/src/drivers/evdev/lv_evdev.h \
		$(STAGING_DIR)/usr/include/lvgl/src/drivers/evdev/lv_evdev.h
endef
LVGL_POST_INSTALL_STAGING_HOOKS += LVGL_INSTALL_LINUX_HEADERS

$(eval $(cmake-package))
