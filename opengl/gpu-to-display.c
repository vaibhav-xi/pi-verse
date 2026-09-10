#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/select.h>
#include <dirent.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define LOG(...)  do { fprintf(stderr, "[gpu_to_panel] " __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define DIE(...)  do { LOG("FATAL: " __VA_ARGS__); exit(1); } while (0)

/* ---- tunables -------------------------------------------------------- */
#define RUN_SECONDS   15.0
#define TARGET_FPS    15

/* Device auto-detection */

/* Finds the first /dev/dri/renderD1xx node. Returns a malloc'd path, or NULL */
static char *find_gpu_render_node(void) {
    DIR *d = opendir("/dev/dri");
    if (!d) DIE("Could not open /dev/dri (%s). Does this system have DRM at all?", strerror(errno));

    struct dirent *entry;
    char *found = NULL;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            found = malloc(256);
            snprintf(found, 256, "/dev/dri/%s", entry->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

/* Finds the first /dev/dri/cardN with a connected connector (our panel). */
static char *find_connected_card(void) {
    DIR *d = opendir("/dev/dri");
    if (!d) DIE("Could not open /dev/dri (%s)", strerror(errno));

    struct dirent *entry;
    char *found = NULL;
    char path[256];

    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "card", 4) != 0) continue;
        snprintf(path, sizeof(path), "/dev/dri/%s", entry->d_name);

        int fd = open(path, O_RDWR);
        if (fd < 0) continue;

        drmModeRes *res = drmModeGetResources(fd);
        if (res) {
            for (int i = 0; i < res->count_connectors; i++) {
                drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
                if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
                    found = strdup(path);
                    drmModeFreeConnector(conn);
                    break;
                }
                if (conn) drmModeFreeConnector(conn);
            }
            drmModeFreeResources(res);
        }
        close(fd);
        if (found) break;
    }
    closedir(d);
    return found;
}

/* GPU side: EGL + GLES2 off-screen rendering */

typedef struct {
    int gpu_fd;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    GLuint fbo;
    GLuint color_tex;
    int width, height;
} GpuCtx;

static const char *egl_error_string(EGLint err) {
    switch (err) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        default: return "UNKNOWN_EGL_ERROR";
    }
}

static void gpu_init(GpuCtx *ctx, const char *render_node, int width, int height) {
    ctx->width = width;
    ctx->height = height;

    ctx->gpu_fd = open(render_node, O_RDWR);
    if (ctx->gpu_fd < 0) DIE("open(%s) failed: %s", render_node, strerror(errno));
    LOG("Opened GPU render node: %s", render_node);

    ctx->gbm = gbm_create_device(ctx->gpu_fd);
    if (!ctx->gbm) DIE("gbm_create_device failed on %s", render_node);

    /* eglGetPlatformDisplay is EGL 1.5 core; EGL_PLATFORM_GBM_KHR from eglext.h */
    ctx->display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ctx->gbm, NULL);
    if (ctx->display == EGL_NO_DISPLAY) DIE("eglGetPlatformDisplay failed: %s", egl_error_string(eglGetError()));

    EGLint major, minor;
    if (!eglInitialize(ctx->display, &major, &minor))
        DIE("eglInitialize failed: %s", egl_error_string(eglGetError()));
    LOG("EGL initialized: version %d.%d", major, minor);
    LOG("EGL_VENDOR:  %s", eglQueryString(ctx->display, EGL_VENDOR));
    LOG("EGL_VERSION: %s", eglQueryString(ctx->display, EGL_VERSION));

    if (!eglBindAPI(EGL_OPENGL_ES_API))
        DIE("eglBindAPI(EGL_OPENGL_ES_API) failed: %s", egl_error_string(eglGetError()));

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(ctx->display, cfg_attribs, &config, 1, &num_configs) || num_configs < 1)
        DIE("eglChooseConfig found no suitable config: %s", egl_error_string(eglGetError()));

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx->context = eglCreateContext(ctx->display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx->context == EGL_NO_CONTEXT) DIE("eglCreateContext failed: %s", egl_error_string(eglGetError()));

    EGLint pbuf_attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    ctx->surface = eglCreatePbufferSurface(ctx->display, config, pbuf_attribs);
    if (ctx->surface == EGL_NO_SURFACE) DIE("eglCreatePbufferSurface failed: %s", egl_error_string(eglGetError()));

    const char *extensions = eglQueryString(ctx->display, EGL_EXTENSIONS);
    LOG("EGL_KHR_surfaceless_context supported: %s",
        (extensions && strstr(extensions, "EGL_KHR_surfaceless_context")) ? "yes" : "NOT ADVERTISED (trying anyway)");

    if (!eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx->context))
        DIE("eglMakeCurrent (surfaceless) failed: %s. This driver may genuinely "
            "require a real surface - tell me and I'll switch to a GBM-backed one.",
            egl_error_string(eglGetError()));

    LOG("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOG("GL_VERSION:  %s", glGetString(GL_VERSION));
    if (strstr((const char *)glGetString(GL_RENDERER), "llvmpipe"))
        LOG("WARNING: GL_RENDERER contains 'llvmpipe' - this is SOFTWARE rendering, "
            "not the real GPU. Check that /dev/dri/renderD1xx really is the V3D "
            "device (vc4-kms-v3d overlay enabled in /boot/firmware/config.txt).");

    glGenTextures(1, &ctx->color_tex);
    glBindTexture(GL_TEXTURE_2D, ctx->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &ctx->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->color_tex, 0);

    GLenum fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE)
        DIE("FBO incomplete: 0x%x", fbo_status);

    glViewport(0, 0, width, height);
    LOG("GPU off-screen FBO ready at %dx%d", width, height);
}

/* Renders one solid-color frame into the FBO and reads it back as RGBA8. */
static void gpu_render_frame(GpuCtx *ctx, float r, float g, float b, uint8_t *out_rgba) {
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    glViewport(0, 0, ctx->width, ctx->height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    glReadPixels(0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
}

static inline uint16_t rgba8_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* GPU's FBO texture is bottom-up (GL convention); the panel wants top-down
 * rows, so this also flips vertically while converting. Writes tightly
 * packed output (no row padding - panel_present handles pitch separately). */
static void convert_rgba_to_rgb565(const uint8_t *rgba, int width, int height, uint8_t *out) {
    uint16_t *out16 = (uint16_t *)out;
    for (int y = 0; y < height; y++) {
        int src_row = height - 1 - y; /* flip */
        const uint8_t *src = rgba + (size_t)src_row * width * 4;
        uint16_t *dst = out16 + (size_t)y * width;
        for (int x = 0; x < width; x++) {
            dst[x] = rgba8_to_rgb565(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2]);
        }
    }
}

static void convert_rgba_to_xrgb8888(const uint8_t *rgba, int width, int height, uint8_t *out) {
    for (int y = 0; y < height; y++) {
        int src_row = height - 1 - y; /* flip */
        const uint8_t *src = rgba + (size_t)src_row * width * 4;
        uint8_t *dst = out + (size_t)y * width * 4;
        for (int x = 0; x < width; x++) {
            /* XRGB8888, little-endian: byte order in memory is B,G,R,X */
            dst[x * 4 + 0] = src[x * 4 + 2]; /* B */
            dst[x * 4 + 1] = src[x * 4 + 1]; /* G */
            dst[x * 4 + 2] = src[x * 4 + 0]; /* R */
            dst[x * 4 + 3] = 0xFF;           /* X */
        }
    }
}

/* Dispatches to the right converter based on what panel_init settled on. */
static void convert_rgba_for_panel(const uint8_t *rgba, int width, int height,
                                    uint32_t drm_format, uint8_t *out) {
    if (drm_format == DRM_FORMAT_RGB565) convert_rgba_to_rgb565(rgba, width, height, out);
    else convert_rgba_to_xrgb8888(rgba, width, height, out);
}

/* Panel side: DRM dumb-buffer double buffering + page flip */

typedef struct {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    uint32_t fb_id[2];
    uint32_t handle[2];
    uint8_t *map[2];
    uint32_t pitch[2];
    int width, height;
    int front;
    uint32_t drm_format;
    int bpp;
} PanelCtx;

static bool try_create_buffer(PanelCtx *p, int slot, uint32_t drm_format, int bpp) {
    struct drm_mode_create_dumb create = {0};
    create.width = p->width;
    create.height = p->height;
    create.bpp = bpp;
    if (drmIoctl(p->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        LOG("  DRM_IOCTL_MODE_CREATE_DUMB (bpp=%d) failed: %s", bpp, strerror(errno));
        return false;
    }

    uint32_t fb_id;
    uint32_t handles[4] = { create.handle, 0, 0, 0 };
    uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(p->fd, p->width, p->height, drm_format,
                       handles, pitches, offsets, &fb_id, 0) < 0) {
        LOG("  drmModeAddFB2 (format=0x%x) failed: %s", drm_format, strerror(errno));
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(p->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return false;
    }

    struct drm_mode_map_dumb map_req = {0};
    map_req.handle = create.handle;
    if (drmIoctl(p->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        LOG("  DRM_IOCTL_MODE_MAP_DUMB failed: %s", strerror(errno));
        drmModeRmFB(p->fd, fb_id);
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(p->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return false;
    }

    uint8_t *map = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, map_req.offset);
    if (map == MAP_FAILED) {
        LOG("  mmap failed: %s", strerror(errno));
        drmModeRmFB(p->fd, fb_id);
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(p->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return false;
    }
    memset(map, 0, create.size);

    p->handle[slot] = create.handle;
    p->pitch[slot] = create.pitch;
    p->fb_id[slot] = fb_id;
    p->map[slot] = map;
    return true;
}

static void panel_init(PanelCtx *p, const char *card_path) {
    p->fd = open(card_path, O_RDWR);
    if (p->fd < 0) DIE("open(%s) failed: %s", card_path, strerror(errno));
    LOG("Opened panel DRM device: %s", card_path);

    drmModeRes *res = drmModeGetResources(p->fd);
    if (!res) DIE("drmModeGetResources failed on %s: %s", card_path, strerror(errno));

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(p->fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) DIE("No connected connector with modes found on %s", card_path);

    p->connector_id = conn->connector_id;
    p->mode = conn->modes[0]; /* preferred/first mode */
    p->width = p->mode.hdisplay;
    p->height = p->mode.vdisplay;
    LOG("Panel connector %u, mode %dx%d@%dHz", p->connector_id, p->width, p->height, p->mode.vrefresh);

    /* Find an encoder/crtc for this connector. */
    drmModeEncoder *enc = NULL;
    if (conn->encoder_id) enc = drmModeGetEncoder(p->fd, conn->encoder_id);
    if (!enc) {
        for (int i = 0; i < conn->count_encoders; i++) {
            enc = drmModeGetEncoder(p->fd, conn->encoders[i]);
            if (enc) break;
        }
    }
    if (!enc) DIE("No encoder found for connector %u", p->connector_id);

    if (enc->crtc_id) {
        p->crtc_id = enc->crtc_id;
    } else {
        drmModeCrtc *found_crtc = NULL;
        for (int i = 0; i < res->count_crtcs; i++) {
            if (enc->possible_crtcs & (1 << i)) {
                p->crtc_id = res->crtcs[i];
                found_crtc = drmModeGetCrtc(p->fd, p->crtc_id);
                if (found_crtc) drmModeFreeCrtc(found_crtc);
                break;
            }
        }
    }
    LOG("Using CRTC %u", p->crtc_id);
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    LOG("Trying RGB565 dumb buffers...");
    if (try_create_buffer(p, 0, DRM_FORMAT_RGB565, 16) &&
        try_create_buffer(p, 1, DRM_FORMAT_RGB565, 16)) {
        p->drm_format = DRM_FORMAT_RGB565;
        p->bpp = 16;
        LOG("Using RGB565.");
    } else {
        LOG("RGB565 didn't work, trying XRGB8888...");
        if (!try_create_buffer(p, 0, DRM_FORMAT_XRGB8888, 32) ||
            !try_create_buffer(p, 1, DRM_FORMAT_XRGB8888, 32))
            DIE("Neither RGB565 nor XRGB8888 dumb buffers worked on this device. "
                "Check `drmModeGetPlane` / driver docs for the panel's actual "
                "supported format.");
        p->drm_format = DRM_FORMAT_XRGB8888;
        p->bpp = 32;
        LOG("Using XRGB8888.");
    }
    LOG("Created double-buffered dumb buffers (pitch=%u, bpp=%d)", p->pitch[0], p->bpp);

    if (drmModeSetCrtc(p->fd, p->crtc_id, p->fb_id[0], 0, 0, &p->connector_id, 1, &p->mode) < 0)
        DIE("drmModeSetCrtc failed: %s", strerror(errno));
    p->front = 0;
    LOG("Initial modeset done - screen should now show black.");
}

static void flip_event_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
    (void)fd; (void)frame; (void)sec; (void)usec;
    *(bool *)data = true;
}

static void panel_present(PanelCtx *p, const uint8_t *packed_pixels) {
    int back = 1 - p->front;
    int bytes_per_pixel = p->bpp / 8;

    for (int y = 0; y < p->height; y++) {
        memcpy(p->map[back] + (size_t)y * p->pitch[back],
               packed_pixels + (size_t)y * p->width * bytes_per_pixel,
               (size_t)p->width * bytes_per_pixel);
    }

    bool flip_done = false;
    if (drmModePageFlip(p->fd, p->crtc_id, p->fb_id[back], DRM_MODE_PAGE_FLIP_EVENT, &flip_done) < 0) {
        LOG("drmModePageFlip failed: %s - falling back to drmModeSetCrtc", strerror(errno));
        if (drmModeSetCrtc(p->fd, p->crtc_id, p->fb_id[back], 0, 0, &p->connector_id, 1, &p->mode) < 0)
            DIE("drmModeSetCrtc fallback also failed: %s", strerror(errno));
        p->front = back;
        return;
    }

    drmEventContext evctx = {0};
    evctx.version = 2;
    evctx.page_flip_handler = flip_event_handler;

    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    fd_set fds;
    while (!flip_done) {
        FD_ZERO(&fds);
        FD_SET(p->fd, &fds);
        int ret = select(p->fd + 1, &fds, NULL, NULL, &timeout);
        if (ret <= 0) { LOG("Timed out waiting for page flip event"); break; }
        drmHandleEvent(p->fd, &evctx);
    }
    p->front = back;
}


int main(int argc, char **argv) {
    const char *gpu_path = NULL;
    const char *panel_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gpu") && i + 1 < argc) gpu_path = argv[++i];
        else if (!strcmp(argv[i], "--panel") && i + 1 < argc) panel_path = argv[++i];
    }

    char *auto_gpu = NULL, *auto_panel = NULL;
    if (!gpu_path) {
        auto_gpu = find_gpu_render_node();
        if (!auto_gpu) DIE("No /dev/dri/renderD1xx found - no GPU-capable DRM driver "
                            "appears to be loaded. Pass --gpu explicitly if you know the path.");
        gpu_path = auto_gpu;
        LOG("Auto-detected GPU render node: %s", gpu_path);
    }
    if (!panel_path) {
        auto_panel = find_connected_card();
        if (!auto_panel) DIE("No /dev/dri/cardN with a connected connector found. "
                              "Pass --panel explicitly. Also make sure Xorg/the desktop "
                              "isn't holding it (see start-piverse.sh).");
        panel_path = auto_panel;
        LOG("Auto-detected panel device: %s", panel_path);
    }

    PanelCtx panel = {0};
    panel_init(&panel, panel_path);

    GpuCtx gpu = {0};
    gpu_init(&gpu, gpu_path, panel.width, panel.height);

    size_t pixel_count = (size_t)panel.width * panel.height;
    uint8_t *rgba_buf = malloc(pixel_count * 4);
    uint8_t *packed_buf = malloc(pixel_count * 4);
    if (!rgba_buf || !packed_buf) DIE("out of memory");

    LOG("Starting %g second red -> green -> blue -> red cycle at %d fps...", RUN_SECONDS, TARGET_FPS);

    int total_frames = (int)(RUN_SECONDS * TARGET_FPS);
    for (int f = 0; f < total_frames; f++) {
        double t = (double)f / total_frames;           /* 0..1 over the whole run */
        double hue = fmod(t * 3.0, 1.0) * 360.0;        /* 3 full cycles */

        /* cheap HSV(hue,1,1)->RGB */
        double c = 1.0, x = 1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0);
        double r1, g1, b1;
        if      (hue < 60)  { r1 = c; g1 = x; b1 = 0; }
        else if (hue < 120) { r1 = x; g1 = c; b1 = 0; }
        else if (hue < 180) { r1 = 0; g1 = c; b1 = x; }
        else if (hue < 240) { r1 = 0; g1 = x; b1 = c; }
        else if (hue < 300) { r1 = x; g1 = 0; b1 = c; }
        else                { r1 = c; g1 = 0; b1 = x; }

        gpu_render_frame(&gpu, (float)r1, (float)g1, (float)b1, rgba_buf);
        convert_rgba_for_panel(rgba_buf, panel.width, panel.height, panel.drm_format, packed_buf);
        panel_present(&panel, packed_buf);

        if (f % TARGET_FPS == 0) LOG("frame %d/%d, hue=%.0f", f, total_frames, hue);
    }

    LOG("Done. If the panel showed a smooth cycling rainbow of color, the "
        "GPU -> panel pipeline works end to end.");

    free(rgba_buf);
    free(packed_buf);
    free(auto_gpu);
    free(auto_panel);
    return 0;
}