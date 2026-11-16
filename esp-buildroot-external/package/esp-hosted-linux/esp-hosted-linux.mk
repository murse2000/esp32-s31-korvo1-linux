################################################################################
#
# esp-hosted-linux
#
################################################################################

ESP_HOSTED_LINUX_VERSION = 48369ec75c9f6fdd8060b8f249705a4f19a5cb45
ESP_HOSTED_LINUX_SITE = https://github.com/espressif/esp-hosted-linux.git
ESP_HOSTED_LINUX_SITE_METHOD = git
ESP_HOSTED_LINUX_LICENSE = GPL-2.0-only
ESP_HOSTED_LINUX_LICENSE_FILES = host/LICENSE

ESP_HOSTED_LINUX_MODULE_SUBDIRS = host
ESP_HOSTED_LINUX_MODULE_MAKE_OPTS = target=usb

$(eval $(kernel-module))
$(eval $(generic-package))
