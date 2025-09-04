VAULTUSB_CPP_VERSION = 1.0.0
VAULTUSB_CPP_SITE = $(BR2_EXTERNAL_VAULTUSB_PATH)/cpp
VAULTUSB_CPP_SITE_METHOD = local
VAULTUSB_CPP_LICENSE = MIT
VAULTUSB_CPP_LICENSE_FILES = ../LICENSE

VAULTUSB_CPP_DEPENDENCIES = sqlite openssl libargon2

VAULTUSB_CPP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr/local

define VAULTUSB_CPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vaultusb_cpp $(TARGET_DIR)/usr/local/bin/vaultusb_cpp
	$(INSTALL) -D -m 0644 $(@D)/../config.toml $(TARGET_DIR)/opt/vaultusb/config.toml
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/vaultusb/templates
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/vaultusb/static
	$(INSTALL) -d -m 0700 $(TARGET_DIR)/opt/vaultusb/vault
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/vaultusb
	$(INSTALL) -D -m 0644 $(@D)/../networking/dnsmasq-uap0.conf $(TARGET_DIR)/etc/vaultusb/
	$(INSTALL) -D -m 0644 $(@D)/../networking/dnsmasq-usb0.conf $(TARGET_DIR)/etc/vaultusb/
	$(INSTALL) -D -m 0644 $(@D)/../networking/hostapd.conf $(TARGET_DIR)/etc/vaultusb/
endef

define VAULTUSB_CPP_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(@D)/../scripts/vaultusb-init $(TARGET_DIR)/etc/init.d/S99vaultusb
	$(INSTALL) -D -m 0755 $(@D)/../scripts/vaultusb-firstboot $(TARGET_DIR)/usr/local/sbin/vaultusb-firstboot.sh
endef

$(eval $(cmake-package))
