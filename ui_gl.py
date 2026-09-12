import ctypes
import os
from pathlib import Path

import requests

import config

BASE_DIR = Path(__file__).resolve().parent

_LIB_CANDIDATES = [
    BASE_DIR / "libpiverse_gl.so",
    BASE_DIR / "opengl" / "libpiverse_gl.so",
]
if os.environ.get("PIVERSE_GL_LIB"):
    _LIB_CANDIDATES.insert(0, Path(os.environ["PIVERSE_GL_LIB"]))


class SyncedLine(ctypes.Structure):
    """Must match pv_api.h's use of lyrics_render.h's SyncedLine exactly:
    struct { long timestamp_ms; const char *text; }"""
    _fields_ = [
        ("timestamp_ms", ctypes.c_long),
        ("text", ctypes.c_char_p),
    ]


def _find_library():
    for path in _LIB_CANDIDATES:
        if path.is_file():
            return str(path)
    raise FileNotFoundError(
        "Could not find libpiverse_gl.so. Looked in:\n  "
        + "\n  ".join(str(p) for p in _LIB_CANDIDATES)
        + "\nBuild it with `make libpiverse_gl.so` in the opengl/ project, "
        "then copy it next to ui_gl.py (or set PIVERSE_GL_LIB)."
    )


def _load_library():
    lib = ctypes.CDLL(_find_library())

    lib.pv_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.pv_init.restype = ctypes.c_int

    lib.pv_set_album_art.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    lib.pv_set_album_art.restype = ctypes.c_int

    lib.pv_clear_album_art.argtypes = []
    lib.pv_clear_album_art.restype = None

    lib.pv_render_frame.argtypes = [
        ctypes.c_bool,                # has_track
        ctypes.c_char_p,              # track_id
        ctypes.c_char_p,              # track_name
        ctypes.c_char_p,              # artist_name
        ctypes.c_long,                # duration_ms
        ctypes.c_long,                # progress_ms
        ctypes.c_bool,                # is_playing
        ctypes.POINTER(SyncedLine),   # synced_lines (NULL ok)
        ctypes.c_int,                 # synced_count
        ctypes.c_char_p,              # plain_lyrics (NULL ok)
        ctypes.c_bool,                # instrumental
        ctypes.c_bool,                # has_lyrics_data
        ctypes.c_bool,                # found
    ]
    lib.pv_render_frame.restype = ctypes.c_int

    lib.pv_shutdown.argtypes = []
    lib.pv_shutdown.restype = None

    return lib


def _encode(text):
    """None-safe UTF-8 encode for ctypes c_char_p fields."""
    return text.encode("utf-8") if text is not None else None


class LyricsUI:
    def __init__(self, windowed=False, fbdev=None, gpu_device=None):
        if windowed:
            print(
                "[ui_gl] windowed mode isn't supported by the GL renderer - "
                "running on the real panel instead. Use the standalone C "
                "diagnostics (lyrics_test) in opengl/ for windowed-style testing."
            )

        self._lib = _load_library()
        self._album_art_url = None
        self._album_art_cache = {}  # url -> bytes, same caching ui.py did

        panel_device = fbdev.encode() if fbdev else None
        gpu_device_enc = gpu_device.encode() if gpu_device else None
        font_path = config.FONT_PATH.encode()

        result = self._lib.pv_init(gpu_device_enc, panel_device, font_path)
        if result != 0:
            raise RuntimeError(
                "pv_init() failed - see the [piverse-gl] FATAL message above for "
                "the exact cause (common ones: desktop still holding the DRM "
                "device - stop display-manager first; vc4-kms-v3d not enabled "
                "in /boot/firmware/config.txt; bad font path)."
            )
        self._closed = False

    def _update_album_art(self, url):
        if url == self._album_art_url:
            return

        self._album_art_url = url
        if not url:
            self._lib.pv_clear_album_art()
            return

        data = self._album_art_cache.get(url)
        if data is None:
            try:
                resp = requests.get(url, timeout=6)
                resp.raise_for_status()
                data = resp.content
                self._album_art_cache[url] = data
            except requests.RequestException as exc:
                print(f"[ui_gl] album art fetch failed: {exc}")
                self._lib.pv_clear_album_art()
                return

        if self._lib.pv_set_album_art(data, len(data)) != 0:
            print("[ui_gl] album art decode failed (unsupported format?)")

    def render(self, snapshot, lyrics_state):
        has_track = bool(snapshot and snapshot.get("track_id"))

        if not has_track:
            self._lib.pv_render_frame(
                False, None, None, None, 0, 0, False,
                None, 0, None, False, False, False,
            )
            return

        self._update_album_art(snapshot.get("album_art_url"))

        synced = (lyrics_state or {}).get("synced") or []
        synced_array = (SyncedLine * len(synced))() if synced else None
        encoded_texts = []  # keeps bytes alive through the call below
        for i, (ts, text) in enumerate(synced):
            encoded = _encode(text)
            encoded_texts.append(encoded)
            synced_array[i].timestamp_ms = int(ts)
            synced_array[i].text = encoded

        has_lyrics_data = bool(lyrics_state)
        plain = (lyrics_state or {}).get("plain")

        self._lib.pv_render_frame(
            True,
            _encode(snapshot.get("track_id")),
            _encode(snapshot.get("track_name", "")),
            _encode(snapshot.get("artist_name", "")),
            int(snapshot.get("duration_ms", 0) or 0),
            int(snapshot.get("progress_ms", 0) or 0),
            bool(snapshot.get("is_playing", False)),
            synced_array,
            len(synced),
            _encode(plain),
            bool((lyrics_state or {}).get("instrumental")),
            has_lyrics_data,
            bool((lyrics_state or {}).get("found")),
        )

    def pump_events(self):
        return True

    def close(self):
        if not self._closed:
            self._lib.pv_shutdown()
            self._closed = True

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
