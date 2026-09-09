# Polls Spotify's Web API for what your account is currently playing

import threading
import time

import spotipy
from spotipy.cache_handler import CacheFileHandler
from spotipy.oauth2 import SpotifyPKCE

import config


class SpotifyState:
    def __init__(self):
        cache_handler = CacheFileHandler(cache_path=config.TOKEN_CACHE_PATH)
        self.auth_manager = SpotifyPKCE(
            client_id=config.SPOTIFY_CLIENT_ID,
            redirect_uri=config.SPOTIFY_REDIRECT_URI,
            scope=config.SPOTIFY_SCOPE,
            cache_handler=cache_handler,
            open_browser=False,
        )
        self.sp = spotipy.Spotify(auth_manager=self.auth_manager)

        self._lock = threading.Lock()
        self._stop = False

        self.track_id = None
        self.track_name = ""
        self.artist_name = ""
        self.album_name = ""
        self.duration_ms = 0
        self.progress_ms = 0
        self.is_playing = False
        self.album_art_url = None
        self._last_poll_monotonic = 0.0

    def ensure_authenticated(self):
        """One-time interactive login. Run this once - after
        that the cached token refreshes itself automatically."""
        
        if self.auth_manager.get_cached_token():
            return

        auth_url = self.auth_manager.get_authorize_url()
        print("\nNo cached Spotify token found.")
        print("Open this URL:\n")
        print(auth_url)
        print(
            "\nAfter you approve access you'll be redirected to a "
            f"{config.SPOTIFY_REDIRECT_URI}?code=... URL."
        )
        print("That page will fail to load - that's expected.")
        response_url = input("Copy the FULL URL from the address bar and paste it here: ").strip()
        code = self.auth_manager.parse_response_code(response_url)
        self.auth_manager.get_access_token(code)
        print("Authenticated. Token cached for future runs.\n")

    def poll_once(self):
        try:
            playback = self.sp.current_playback()
        except Exception as exc:
            print(f"[spotify] poll error: {exc}")
            return False

        with self._lock:
            self._last_poll_monotonic = time.monotonic()

            if not playback or not playback.get("item"):
                changed = self.track_id is not None
                self.track_id = None
                self.is_playing = False
                return changed

            item = playback["item"]
            new_id = item.get("id")
            changed = new_id != self.track_id

            self.track_id = new_id
            self.track_name = item.get("name", "")
            self.artist_name = ", ".join(a["name"] for a in item.get("artists", []))
            self.album_name = item.get("album", {}).get("name", "")
            self.duration_ms = item.get("duration_ms", 0)
            self.progress_ms = playback.get("progress_ms") or 0
            self.is_playing = playback.get("is_playing", False)

            images = item.get("album", {}).get("images", [])
            self.album_art_url = images[0]["url"] if images else None

            return changed

    def get_snapshot(self):
        with self._lock:
            progress = self.progress_ms
            if self.is_playing and self._last_poll_monotonic:
                elapsed_ms = (time.monotonic() - self._last_poll_monotonic) * 1000
                progress = min(progress + elapsed_ms, self.duration_ms or progress)
            return {
                "track_id": self.track_id,
                "track_name": self.track_name,
                "artist_name": self.artist_name,
                "album_name": self.album_name,
                "duration_ms": self.duration_ms,
                "progress_ms": progress,
                "is_playing": self.is_playing,
                "album_art_url": self.album_art_url,
            }

    def run_poll_loop(self, on_track_change):
        while not self._stop:
            try:
                changed = self.poll_once()
                if changed:
                    on_track_change(self.get_snapshot())
            except Exception as exc:  # noqa: BLE001
                print(f"[spotify] poll loop error: {exc}")
            time.sleep(config.POLL_INTERVAL_SEC)

    def stop(self):
        self._stop = True
