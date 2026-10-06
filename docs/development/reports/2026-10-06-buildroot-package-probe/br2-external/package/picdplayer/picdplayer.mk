################################################################################
#
# picdplayer (local source feasibility probe)
#
################################################################################

PICDPLAYER_VERSION = 0.1.0-probe
PICDPLAYER_SITE = /src
PICDPLAYER_SITE_METHOD = local
PICDPLAYER_LICENSE = UNKNOWN
PICDPLAYER_DEPENDENCIES = alsa-lib libcurl libdiscid libwebsockets json-for-modern-cpp
PICDPLAYER_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DENABLE_METADATA=ON \
	-DENABLE_API=ON \
	-DENABLE_PARANOIA=OFF \
	-DBUILD_TESTING=OFF \
	-DINSTALL_SYSTEMD_UNIT=ON \
	-DINSTALL_SYSTEMD_KIOSK_UNIT=OFF \
	-DPICDPLAYER_SYSTEMD_UNIT_DIR=lib/systemd/system

$(eval $(cmake-package))
