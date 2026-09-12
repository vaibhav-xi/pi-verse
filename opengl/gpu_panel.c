#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "gpu_panel.h"

bool g_pv_use_longjmp = false;
jmp_buf g_pv_error_jmp;


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/select.h>
#include <dirent.h>

#include <xf86drm.h>
#include <drm_fourcc.h>
#include <EGL/eglext.h>
#include "device_select.h"

/* ---- device auto-detection ---- */

char *find_gpu_render_node(void) {
    DIR *d = opendir("/dev/dri");
    if (!d) DIE("Could not open /dev/dri (%s). Does this system have DRM at all?", strerror(errno));

    RenderNodeCandidate candidates[MAX_DEVICE_CANDIDATES] = {0};
    int count = 0;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && count < MAX_DEVICE_CANDIDATES) {
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            snprintf(candidates[count].path, sizeof(candidates[count].path), "/dev/dri/%s", entry->d_name);
            count++;
        }
    }
    closedir(d);

    LOG("GPU render nodes found: %d", count);
    for (int i = 0; i < count; i++) LOG("  %s", candidates[i].path);

    const char *forced = getenv("PIVERSE_GPU_DEVICE");
    if (forced && forced[0]) LOG("PIVERSE_GPU_DEVICE override: %s", forced);

    int idx = select_render_node_candidate(candidates, count, forced);
    if (idx < 0) return NULL;
    return strdup(candidates[idx].path);
}

/* Best-effort driver name for logging (e.g. "vc4", "v3d", "ili9486") -
 * never fails the caller, just falls back to "unknown". */
static void get_driver_name(int fd, char *out, size_t out_size) {
    drmVersionPtr ver = drmGetVersion(fd);
    if (ver && ver->name) {
        snprintf(out, out_size, "%.*s", ver->name_len, ver->name);
        drmFreeVersion(ver);
    } else {
        snprintf(out, out_size, "unknown");
    }
}

char *find_connected_card(void) {
    DIR *d = opendir("/dev/dri");
    if (!d) DIE("Could not open /dev/dri (%s)", strerror(errno));

    DisplayCandidate candidates[MAX_DEVICE_CANDIDATES] = {0};
    int count = 0;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && count < MAX_DEVICE_CANDIDATES) {
        if (strncmp(entry->d_name, "card", 4) != 0) continue;

        char path[256];
        snprintf(path, sizeof(path), "/dev/dri/%s", entry->d_name);

        int fd = open(path, O_RDWR);
        if (fd < 0) continue;

        drmModeRes *res = drmModeGetResources(fd);
        bool added_any = false;
        if (res) {
            for (int i = 0; i < res->count_connectors && count < MAX_DEVICE_CANDIDATES; i++) {
                drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
                if (!conn) continue;

                bool connected = (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0);
                
                if (connected) {
                    snprintf(candidates[count].path, sizeof(candidates[count].path), "%s", path);
                    candidates[count].connected = true;
                    candidates[count].width = conn->modes[0].hdisplay;
                    candidates[count].height = conn->modes[0].vdisplay;
                    get_driver_name(fd, candidates[count].driver_name, sizeof(candidates[count].driver_name));
                    count++;
                    added_any = true;
                }
                drmModeFreeConnector(conn);
            }
            drmModeFreeResources(res);
        }

        if (!added_any && count < MAX_DEVICE_CANDIDATES) {
            snprintf(candidates[count].path, sizeof(candidates[count].path), "%s", path);
            candidates[count].connected = false;
            candidates[count].width = candidates[count].height = 0;
            get_driver_name(fd, candidates[count].driver_name, sizeof(candidates[count].driver_name));
            count++;
        }
        close(fd);
    }
    closedir(d);

    LOG("Display cards found: %d", count);
    for (int i = 0; i < count; i++) {
        if (candidates[i].connected)
            LOG("  %s [%s] connected, %dx%d", candidates[i].path, candidates[i].driver_name,
                candidates[i].width, candidates[i].height);
        else
            LOG("  %s [%s] not connected", candidates[i].path, candidates[i].driver_name);
    }

    const char *forced = getenv("PIVERSE_PANEL_DEVICE");
    if (forced && forced[0]) LOG("PIVERSE_PANEL_DEVICE override: %s", forced);

    int connected_count = 0;
    for (int i = 0; i < count; i++) if (candidates[i].connected) connected_count++;
    if (connected_count > 1)
        LOG("WARNING: %d displays connected simultaneously - picking the first one found. "
            "Set PIVERSE_PANEL_DEVICE or pass --panel to force a specific one.", connected_count);

    int idx = select_display_candidate(candidates, count, forced);
    if (idx < 0) return NULL;
    return strdup(candidates[idx].path);
}

/* ---- GPU ---- */

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

void gpu_init(GpuCtx *ctx, const char *render_node, int width, int height) {
    ctx->width = width;
    ctx->height = height;

    ctx->gpu_fd = open(render_node, O_RDWR);
    if (ctx->gpu_fd < 0) DIE("open(%s) failed: %s", render_node, strerror(errno));
    LOG("Opened GPU render node: %s", render_node);

    ctx->gbm = gbm_create_device(ctx->gpu_fd);
    if (!ctx->gbm) DIE("gbm_create_device failed on %s", render_node);

    ctx->display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ctx->gbm, NULL);
    if (ctx->display == EGL_NO_DISPLAY) {
        LOG("eglGetPlatformDisplay (EGL 1.5 core) unavailable, trying "
            "EGL_EXT_platform_base fallback (older Mesa)...");
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display_ext =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (get_platform_display_ext)
            ctx->display = get_platform_display_ext(EGL_PLATFORM_GBM_KHR, ctx->gbm, NULL);
    }
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
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(ctx->display, cfg_attribs, &config, 1, &num_configs) || num_configs < 1)
        DIE("eglChooseConfig found no suitable config: %s", egl_error_string(eglGetError()));

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx->context = eglCreateContext(ctx->display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx->context == EGL_NO_CONTEXT) DIE("eglCreateContext failed: %s", egl_error_string(eglGetError()));

    ctx->surface = EGL_NO_SURFACE;

    const char *extensions = eglQueryString(ctx->display, EGL_EXTENSIONS);
    LOG("EGL_KHR_surfaceless_context supported: %s",
        (extensions && strstr(extensions, "EGL_KHR_surfaceless_context")) ? "yes" : "NOT ADVERTISED (trying anyway)");

    if (!eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx->context))
        DIE("eglMakeCurrent (surfaceless) failed: %s", egl_error_string(eglGetError()));

    const char *renderer_str = (const char *)glGetString(GL_RENDERER);
    LOG("GL_RENDERER: %s", renderer_str);
    LOG("GL_VERSION:  %s", glGetString(GL_VERSION));
    if (strstr(renderer_str, "V3D"))
        LOG("Real GPU acceleration confirmed (V3D - Pi 4/5-class VideoCore VI GPU).");
    else if (strstr(renderer_str, "VC4"))
        LOG("Real GPU acceleration confirmed (VC4 - Pi 0-3-class VideoCore IV GPU, "
            "noticeably weaker than V3D but plenty for this UI's workload).");
    else if (strstr(renderer_str, "llvmpipe"))
        LOG("WARNING: GL_RENDERER contains 'llvmpipe' - this is SOFTWARE rendering, "
            "not the real GPU. Check that vc4-kms-v3d (or the Pi 3 equivalent "
            "vc4-kms overlay) is enabled in /boot/firmware/config.txt.");

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

void gpu_begin_frame(GpuCtx *ctx) {
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    glViewport(0, 0, ctx->width, ctx->height);
}

void gpu_end_frame(GpuCtx *ctx, uint8_t *out_rgba) {
    glFinish();
    glReadPixels(0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
}

void gpu_render_solid_frame(GpuCtx *ctx, float r, float g, float b, uint8_t *out_rgba) {
    gpu_begin_frame(ctx);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    gpu_end_frame(ctx, out_rgba);
}

/* ---- pixel format conversion ---- */

static inline uint16_t rgba8_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void convert_rgba_to_rgb565(const uint8_t *rgba, int width, int height, uint8_t *out) {
    uint16_t *out16 = (uint16_t *)out;
    for (int y = 0; y < height; y++) {
        int src_row = height - 1 - y; /* GL FBO is bottom-up */
        const uint8_t *src = rgba + (size_t)src_row * width * 4;
        uint16_t *dst = out16 + (size_t)y * width;
        for (int x = 0; x < width; x++)
            dst[x] = rgba8_to_rgb565(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2]);
    }
}

static void convert_rgba_to_xrgb8888(const uint8_t *rgba, int width, int height, uint8_t *out) {
    for (int y = 0; y < height; y++) {
        int src_row = height - 1 - y;
        const uint8_t *src = rgba + (size_t)src_row * width * 4;
        uint8_t *dst = out + (size_t)y * width * 4;
        for (int x = 0; x < width; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2]; /* B */
            dst[x * 4 + 1] = src[x * 4 + 1]; /* G */
            dst[x * 4 + 2] = src[x * 4 + 0]; /* R */
            dst[x * 4 + 3] = 0xFF;           /* X */
        }
    }
}

void convert_rgba_for_panel(const uint8_t *rgba, int width, int height,
                             uint32_t drm_format, uint8_t *out) {
    if (drm_format == DRM_FORMAT_RGB565) convert_rgba_to_rgb565(rgba, width, height, out);
    else convert_rgba_to_xrgb8888(rgba, width, height, out);
}

/* ---- panel ---- */

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

void panel_init(PanelCtx *p, const char *card_path) {
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
    p->mode = conn->modes[0];
    p->width = p->mode.hdisplay;
    p->height = p->mode.vdisplay;
    LOG("Panel connector %u, mode %dx%d@%dHz", p->connector_id, p->width, p->height, p->mode.vrefresh);
    if (conn->mmWidth > 0 && conn->mmHeight > 0)
        LOG("Physical size from EDID: %umm x %umm (informational only - not used for UI "
            "scaling, since many small SPI panels report 0 or inaccurate values here)",
            conn->mmWidth, conn->mmHeight);

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
        for (int i = 0; i < res->count_crtcs; i++) {
            if (enc->possible_crtcs & (1 << i)) {
                p->crtc_id = res->crtcs[i];
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
            DIE("Neither RGB565 nor XRGB8888 dumb buffers worked on this device.");
        p->drm_format = DRM_FORMAT_XRGB8888;
        p->bpp = 32;
        LOG("Using XRGB8888.");
    }
    LOG("Created double-buffered dumb buffers (pitch=%u, bpp=%d)", p->pitch[0], p->bpp);

    if (drmModeSetCrtc(p->fd, p->crtc_id, p->fb_id[0], 0, 0, &p->connector_id, 1, &p->mode) < 0)
        DIE("drmModeSetCrtc failed: %s", strerror(errno));
    p->front = 0;
    LOG("Initial modeset done.");
}

static void flip_event_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
    (void)fd; (void)frame; (void)sec; (void)usec;
    *(bool *)data = true;
}

void panel_present(PanelCtx *p, const uint8_t *packed_pixels) {
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
