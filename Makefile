CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS += -Wall -Wextra -Wno-format-truncation -O2 -std=c11 $(shell $(PKG_CONFIG) --cflags libdrm egl glesv2 gbm)
LDLIBS += -lm $(shell $(PKG_CONFIG) --libs libdrm egl glesv2 gbm)

TARGET = gpu-to-display

all: $(TARGET)

$(TARGET): gpu_to_panel_test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
