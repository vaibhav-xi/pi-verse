CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS += -Wall -Wextra -Wno-format-truncation -O2 -fPIC -std=c11 $(shell $(PKG_CONFIG) --cflags libdrm egl glesv2 gbm)
LDLIBS += -lm $(shell $(PKG_CONFIG) --libs libdrm egl glesv2 gbm)

# stb_truetype.h/stb_image.h are big single-header libs; keep warnings
# from them out of our own -Wall/-Wextra build with a relaxed flag set.
FONT_CFLAGS = -Wall -O2 -fPIC -std=c11 $(shell $(PKG_CONFIG) --cflags libdrm egl glesv2 gbm)

all: gpu_to_panel_test text_test lyrics_test libpiverse_gl.so

gpu_panel.o: gpu_panel.c gpu_panel.h
	$(CC) $(CFLAGS) -c -o $@ gpu_panel.c

font.o: font.c font.h stb_truetype.h
	$(CC) $(FONT_CFLAGS) -c -o $@ font.c

image.o: image.c image.h stb_image.h
	$(CC) $(FONT_CFLAGS) -c -o $@ image.c

lyrics_layout.o: lyrics_layout.c lyrics_layout.h
	$(CC) $(CFLAGS) -c -o $@ lyrics_layout.c

lyrics_render.o: lyrics_render.c lyrics_render.h lyrics_layout.h font.h image.h
	$(CC) $(CFLAGS) -c -o $@ lyrics_render.c

pv_api.o: pv_api.c pv_api.h gpu_panel.h lyrics_render.h
	$(CC) $(CFLAGS) -c -o $@ pv_api.c

libpiverse_gl.so: pv_api.o gpu_panel.o font.o image.o lyrics_layout.o lyrics_render.o
	$(CC) -shared -o $@ pv_api.o gpu_panel.o font.o image.o lyrics_layout.o lyrics_render.o $(LDLIBS)

gpu_to_panel_test: gpu_to_panel_test.c gpu_panel.o
	$(CC) $(CFLAGS) -o $@ gpu_to_panel_test.c gpu_panel.o $(LDLIBS)

text_test: text_test.c gpu_panel.o font.o
	$(CC) $(CFLAGS) -o $@ text_test.c gpu_panel.o font.o $(LDLIBS)

lyrics_test: lyrics_test.c gpu_panel.o font.o image.o lyrics_layout.o lyrics_render.o
	$(CC) $(CFLAGS) -o $@ lyrics_test.c gpu_panel.o font.o image.o lyrics_layout.o lyrics_render.o $(LDLIBS)

# Pure logic, no DRM/EGL/GLES needed - runs anywhere, including off-Pi.
test_lyrics_layout: test_lyrics_layout.c lyrics_layout.c
	$(CC) -Wall -Wextra -Wno-format-truncation -O2 -std=c11 -o $@ test_lyrics_layout.c lyrics_layout.c -lm

test: test_lyrics_layout
	./test_lyrics_layout

clean:
	rm -f gpu_to_panel_test text_test lyrics_test test_lyrics_layout libpiverse_gl.so *.o

.PHONY: all clean test
