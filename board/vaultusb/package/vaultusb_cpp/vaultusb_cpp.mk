################################################################################
# vaultusb_cpp
################################################################################

VAULTUSB_CPP_VERSION = 1.0
VAULTUSB_CPP_SITE = $(TOPDIR)/../..
VAULTUSB_CPP_SITE_METHOD = local
VAULTUSB_CPP_LICENSE = MIT

# Use CMake to build from the repository root (top-level CMakeLists adds subdir)
VAULTUSB_CPP_SUPPORTS_IN_SOURCE_BUILD = NO

$(eval $(cmake-package))

