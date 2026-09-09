# Fullscreen pygame renderer. In production it draws straight to the SPI display's framebuffer device (no X11 needed);

import io
import os

import pygame
import requests

import config

BG_COLOR = (10, 10, 14)
TEXT_COLOR = (240, 240, 245)
DIM_COLOR = (110, 110, 120)
ACCENT_COLOR = (30, 215, 96)  # Spotify green
LINE_GAP = 34
ART_SIZE = 108


class LyricsUI:
    def __init__(self, windowed=False, fbdev=None):
        if not windowed:
            os.environ.setdefault("SDL_VIDEODRIVER", "fbcon")
            os.environ.setdefault("SDL_FBDEV", fbdev or "/dev/fb1")
            os.environ.setdefault("SDL_NOMOUSE", "1")

        pygame.init()
        pygame.mouse.set_visible(False)
        flags = 0 if windowed else pygame.FULLSCREEN
        size = (config.SCREEN_WIDTH, config.SCREEN_HEIGHT)
        self.screen = pygame.display.set_mode(size, flags)
        self.clock = pygame.time.Clock()

        self.font_title = self._load_font(20, bold=True)
        self.font_artist = self._load_font(15)
        self.font_lyric_active = self._load_font(22, bold=True)
        self.font_lyric_dim = self._load_font(18)

        self._art_cache = {}
        self._current_art_url = None
        self._current_art_surface = None

    def _load_font(self, size, bold=False):
        try:
            font = pygame.font.Font(config.FONT_PATH, size)
        except (FileNotFoundError, OSError):
            font = pygame.font.SysFont("dejavusans", size, bold=bold)
        font.set_bold(bold)
        return font

    def _get_album_art(self, url):
        if not url:
            return None
        if url == self._current_art_url:
            return self._current_art_surface
        if url in self._art_cache:
            self._current_art_url = url
            self._current_art_surface = self._art_cache[url]
            return self._current_art_surface

        surface = None
        try:
            resp = requests.get(url, timeout=6)
            resp.raise_for_status()
            surface = pygame.image.load(io.BytesIO(resp.content)).convert()
            surface = pygame.transform.smoothscale(surface, (ART_SIZE, ART_SIZE))
        except Exception as exc:  # noqa: BLE001
            print(f"[ui] album art fetch failed: {exc}")

        self._art_cache[url] = surface
        self._current_art_url = url
        self._current_art_surface = surface
        return surface

    @staticmethod
    def _current_lyric_index(synced_lyrics, progress_ms):
        idx = -1
        for i, (ts, _text) in enumerate(synced_lyrics):
            if ts <= progress_ms:
                idx = i
            else:
                break
        return idx

    def _draw_center(self, text, font, color, center_x, center_y):
        surf = font.render(text, True, color)
        rect = surf.get_rect(center=(center_x, center_y))
        self.screen.blit(surf, rect)

    def render(self, snapshot, lyrics_state):
        self.screen.fill(BG_COLOR)
        w, h = config.SCREEN_WIDTH, config.SCREEN_HEIGHT

        if not snapshot or not snapshot.get("track_id"):
            self._draw_center("Nothing playing", self.font_title, DIM_COLOR, w // 2, h // 2)
            pygame.display.flip()
            self.clock.tick(30)
            return

        art_surface = self._get_album_art(snapshot.get("album_art_url"))
        if art_surface:
            self.screen.blit(art_surface, (12, 12))

        text_x = 12 + ART_SIZE + 14 if art_surface else 16
        title_surf = self.font_title.render(snapshot["track_name"], True, TEXT_COLOR)
        self.screen.blit(title_surf, (text_x, 20))
        artist_surf = self.font_artist.render(snapshot["artist_name"], True, DIM_COLOR)
        self.screen.blit(artist_surf, (text_x, 48))

        header_bottom = 12 + ART_SIZE + 10
        pygame.draw.line(self.screen, (40, 40, 46), (0, header_bottom), (w, header_bottom), 1)

        lyrics_top = header_bottom
        center_y = lyrics_top + (h - lyrics_top) // 2
        synced = lyrics_state.get("synced") if lyrics_state else []

        if lyrics_state and lyrics_state.get("instrumental"):
            self._draw_center("(Instrumental)", self.font_lyric_dim, DIM_COLOR, w // 2, center_y)
        elif synced:
            idx = self._current_lyric_index(synced, snapshot["progress_ms"])
            for offset in range(-2, 3):
                i = idx + offset
                if 0 <= i < len(synced):
                    _, text = synced[i]
                    if offset == 0:
                        font, color = self.font_lyric_active, ACCENT_COLOR
                    else:
                        font, color = self.font_lyric_dim, DIM_COLOR
                    self._draw_center(text, font, color, w // 2, center_y + offset * LINE_GAP)
        elif lyrics_state and lyrics_state.get("plain"):
            self._draw_center("Lyrics found (not time-synced)", self.font_lyric_dim,
                               DIM_COLOR, w // 2, center_y)
        elif lyrics_state and lyrics_state.get("found") is False:
            self._draw_center("No lyrics found", self.font_lyric_dim, DIM_COLOR, w // 2, center_y)
        else:
            self._draw_center("Loading lyrics...", self.font_lyric_dim, DIM_COLOR, w // 2, center_y)

        pygame.display.flip()
        self.clock.tick(30)

    def pump_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return False
            if event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                return False
        return True
