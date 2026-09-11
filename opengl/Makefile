CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS += -Wall -Wextra -Wno-format-truncation -O2 -std=c11 $(shell $(PKG_CONFIG) --cflags libdrm egl glesv2 gbm)
LDLIBS += -lm $(shell $(PKG_CONFIG) --libs libdrm egl glesv2 gbm)

# stb_truetype.h is a big single-header lib; keep warnings from it out of
# our own -Wall/-Wextra build by compiling font.c with a relaxed flag set.
FONT_CFLAGS = -Wall -O2 -std=c11 $(shell $(PKG_CONFIG) --cflags libdrm egl glesv2 gbm)

all: gpu_to_panel_test text_test

gpu_panel.o: gpu_panel.c gpu_panel.h
	$(CC) $(CFLAGS) -c -o $@ gpu_panel.c

font.o: font.c font.h stb_truetype.h
	$(CC) $(FONT_CFLAGS) -c -o $@ font.c

gpu_to_panel_test: gpu_to_panel_test.c gpu_panel.o
	$(CC) $(CFLAGS) -o $@ gpu_to_panel_test.c gpu_panel.o $(LDLIBS)

text_test: text_test.c gpu_panel.o font.o
	$(CC) $(CFLAGS) -o $@ text_test.c gpu_panel.o font.o $(LDLIBS)

clean:
	rm -f gpu_to_panel_test text_test *.o

.PHONY: all clean
