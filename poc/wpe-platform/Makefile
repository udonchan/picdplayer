CC ?= cc
CFLAGS += -std=c11 -Wall -Wextra -Werror
WPE_PACKAGES = wpe-platform-drm-2.0 wpe-webkit-2.0

all: picdplayer-wpe-poc

picdplayer-wpe-poc: main.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(WPE_PACKAGES)) -o $@ $< $(shell pkg-config --libs $(WPE_PACKAGES))

clean:
	rm -f picdplayer-wpe-poc

.PHONY: all clean
