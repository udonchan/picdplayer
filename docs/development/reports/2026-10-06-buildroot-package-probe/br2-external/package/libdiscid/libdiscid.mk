################################################################################
#
# libdiscid (PiCDPlayer feasibility probe)
#
################################################################################

LIBDISCID_VERSION = 0.7.0
LIBDISCID_SITE = https://data.metabrainz.org/pub/musicbrainz/libdiscid
LIBDISCID_SOURCE = libdiscid-$(LIBDISCID_VERSION).tar.gz
LIBDISCID_LICENSE = LGPL-2.1+
LIBDISCID_LICENSE_FILES = COPYING
LIBDISCID_INSTALL_STAGING = YES

$(eval $(cmake-package))
