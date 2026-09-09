import io
import os
import re

import pygame
import requests

import config


BG_COLOR = (10, 10, 14)
TEXT_COLOR = (240, 240, 245)
DIM_COLOR = (110, 110, 120)
ACCENT_COLOR = (30, 215, 96)

ART_SIZE = 108

# Normal distance between separate lyric lines.
VERSE_GAP = 20

# Smaller distance between wrapped parts of the same lyric.
WRAPPED_GAP = 22

# Maximum width available for lyrics.
LYRIC_MARGIN = 12

TITLE_MAX_CHARS = 29
TITLE_SCROLL_SPEED = 18
TITLE_SCROLL_PAUSE = 3
TITLE_SCROLL_GAP = 45


class LyricsUI:
    def __init__(self, windowed=False, fbdev=None):
        self.windowed = windowed

        if windowed:
            pygame.init()
            size = (
                config.SCREEN_WIDTH,
                config.SCREEN_HEIGHT,
            )

            try:
                self.screen = pygame.display.set_mode(size)
            except pygame.error as exc:
                raise SystemExit(
                    f"Could not open a window ({exc}).\n"
                    "If you're on SSH without a desktop or X11 "
                    "forwarding, run without --windowed on the Pi."
                ) from exc

        else:
            os.environ["SDL_VIDEODRIVER"] = "kmsdrm"

            pygame.init()

            size = (
                config.SCREEN_WIDTH,
                config.SCREEN_HEIGHT,
            )

            try:
                self.screen = pygame.display.set_mode(
                    size,
                    pygame.FULLSCREEN,
                )
            except pygame.error as exc:
                raise SystemExit(
                    f"Could not open the LCD through KMSDRM: {exc}\n"
                    "Make sure the graphical desktop is stopped and "
                    "that /dev/dri/card0 is available."
                ) from exc

        pygame.mouse.set_visible(False)

        self.clock = pygame.time.Clock()

        self.font_title = self._load_font(20, bold=True)
        self.font_artist = self._load_font(15)

        self.font_lyric_active = self._load_font(
            22,
            bold=True,
        )

        self.font_lyric_dim = self._load_font(
            18,
        )

        self._art_cache = {}
        self._current_art_url = None
        self._current_art_surface = None
        
        self._title_track_id = None
        self._title_scroll_x = 0.0
        self._title_scroll_started = 0.0
        self._title_scroll_pause_until = 0.0
        self._title_scroll_direction = 1

    def _present(self):
        pygame.display.flip()

        # Full-frame SPI updates are expensive.
        self.clock.tick(15)

    def _load_font(self, size, bold=False):
        try:
            font = pygame.font.Font(
                config.FONT_PATH,
                size,
            )
        except (FileNotFoundError, OSError):
            font = pygame.font.SysFont(
                "dejavusans",
                size,
                bold=bold,
            )

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
            resp = requests.get(
                url,
                timeout=6,
            )

            resp.raise_for_status()

            surface = pygame.image.load(
                io.BytesIO(resp.content)
            ).convert()

            surface = pygame.transform.smoothscale(
                surface,
                (ART_SIZE, ART_SIZE),
            )

        except Exception as exc:
            print(
                f"[ui] album art fetch failed: {exc}"
            )

        self._art_cache[url] = surface
        self._current_art_url = url
        self._current_art_surface = surface

        return surface

    @staticmethod
    def _current_lyric_index(
        synced_lyrics,
        progress_ms,
    ):
        idx = -1

        for i, (ts, _text) in enumerate(
            synced_lyrics
        ):
            if ts <= progress_ms:
                idx = i
            else:
                break

        return idx

    def _draw_center(
        self,
        text,
        font,
        color,
        center_x,
        center_y,
    ):
        surf = font.render(
            text,
            True,
            color,
        )

        rect = surf.get_rect(
            center=(center_x, center_y)
        )

        self.screen.blit(
            surf,
            rect,
        )

    # LYRIC WRAPPING

    def _split_lyric(self, text, font):

        text = text.strip()

        if not text:
            return []

        max_width = (
            config.SCREEN_WIDTH
            - LYRIC_MARGIN * 2
        )

        # Already fits.
        if font.size(text)[0] <= max_width:
            return [text]

        protected = []
        pattern = re.compile(
            r"\([^()]*\)|"
            r"\[[^\[\]]*\]|"
            r"\{[^{}]*\}"
        )

        last = 0

        for match in pattern.finditer(text):
            if match.start() > last:
                protected.append(
                    ("text", text[last:match.start()])
                )

            protected.append(
                ("group", match.group())
            )

            last = match.end()

        if last < len(text):
            protected.append(
                ("text", text[last:])
            )

        pieces = []

        for kind, value in protected:
            if kind == "group":
                pieces.append(value)
                continue

            parts = re.split(
                r"(?<=[,/])\s+|(?<=[\)\]\}])\s+",
                value,
            )

            for part in parts:
                part = part.strip()

                if part:
                    pieces.append(part)

        # Build lines.

        lines = []
        current = ""

        for piece in pieces:

            candidate = (
                piece
                if not current
                else f"{current} {piece}"
            )

            if font.size(candidate)[0] <= max_width:
                current = candidate
                continue

            # The piece doesn't fit.
            if current:
                lines.append(current)

            # If the piece itself fits, start the next line with it.
            if font.size(piece)[0] <= max_width:
                current = piece

            else:
                # Extremely long piece/group.
                wrapped = self._word_wrap(
                    piece,
                    font,
                    max_width,
                )

                if wrapped:
                    lines.extend(wrapped[:-1])
                    current = wrapped[-1]
                else:
                    current = ""

        if current:
            lines.append(current)

        return lines

    @staticmethod
    def _word_wrap(
        text,
        font,
        max_width,
    ):

        words = text.split()

        lines = []
        current = ""

        for word in words:
            candidate = (
                word
                if not current
                else f"{current} {word}"
            )

            if font.size(candidate)[0] <= max_width:
                current = candidate
                continue

            if current:
                lines.append(current)

            # Extremely long individual word.
            if font.size(word)[0] > max_width:
                chunk = ""

                for char in word:
                    candidate = chunk + char

                    if (
                        font.size(candidate)[0]
                        <= max_width
                    ):
                        chunk = candidate
                    else:
                        if chunk:
                            lines.append(chunk)

                        chunk = char

                current = chunk
            else:
                current = word

        if current:
            lines.append(current)

        return lines

    def _lyric_lines(self, text, font):

        lines = self._split_lyric(text, font)

        if not lines:
            return [], 0

        line_height = font.get_linesize()

        # Tight spacing inside one lyric.
        line_spacing = max(
            line_height - 3,
            WRAPPED_GAP,
        )

        total_height = (
            line_height
            + (len(lines) - 1) * line_spacing
        )

        return lines, total_height


    def _draw_lyric_block(
        self,
        text,
        font,
        color,
        center_y,
    ):

        lines, total_height = self._lyric_lines(
            text,
            font,
        )

        if not lines:
            return total_height

        line_height = font.get_linesize()

        line_spacing = max(
            line_height - 3,
            WRAPPED_GAP,
        )

        y = (
            center_y
            - total_height / 2
            + line_height / 2
        )

        for line in lines:
            self._draw_center(
                line,
                font,
                color,
                config.SCREEN_WIDTH // 2,
                int(y),
            )

            y += line_spacing

        return total_height

    # RENDER

    def render(
        self,
        snapshot,
        lyrics_state,
    ):
        self.screen.fill(BG_COLOR)

        w = config.SCREEN_WIDTH
        h = config.SCREEN_HEIGHT

        if (
            not snapshot
            or not snapshot.get("track_id")
        ):
            self._draw_center(
                "Nothing playing",
                self.font_title,
                DIM_COLOR,
                w // 2,
                h // 2,
            )

            self._present()
            return

        # HEADER

        art_surface = self._get_album_art(
            snapshot.get("album_art_url")
        )

        if art_surface:
            self.screen.blit(
                art_surface,
                (12, 12),
            )

        text_x = (
            12 + ART_SIZE + 14
            if art_surface
            else 16
        )

        self._draw_title(
            snapshot,
            text_x,
            20,
        )

        artist_surf = self.font_artist.render(
            snapshot["artist_name"],
            True,
            DIM_COLOR,
        )

        self.screen.blit(
            artist_surf,
            (text_x, 48),
        )

        header_bottom = (
            12 + ART_SIZE + 10
        )

        pygame.draw.line(
            self.screen,
            (40, 40, 46),
            (0, header_bottom),
            (w, header_bottom),
            1,
        )

        lyrics_top = header_bottom

        center_y = (
            lyrics_top
            + (h - lyrics_top) // 2
        )

        synced = (
            lyrics_state.get("synced")
            if lyrics_state
            else []
        )

        # -----------------------------------------------------
        # LYRICS
        # -----------------------------------------------------

        if (
            lyrics_state
            and lyrics_state.get("instrumental")
        ):
            self._draw_center(
                "(Instrumental)",
                self.font_lyric_dim,
                DIM_COLOR,
                w // 2,
                center_y,
            )

        elif synced:
            idx = self._current_lyric_index(
                synced,
                snapshot["progress_ms"],
            )

            if idx >= 0:

                visible = []

                for offset in range(-2, 3):
                    i = idx + offset

                    if not (0 <= i < len(synced)):
                        continue

                    _, text = synced[i]

                    if offset == 0:
                        font = self.font_lyric_active
                        color = ACCENT_COLOR
                    else:
                        font = self.font_lyric_dim
                        color = DIM_COLOR

                    lines, height = self._lyric_lines(
                        text,
                        font,
                    )

                    if not lines:
                        continue

                    visible.append(
                        {
                            "offset": offset,
                            "text": text,
                            "font": font,
                            "color": color,
                            "height": height,
                        }
                    )

                total_height = sum(
                    item["height"]
                    for item in visible
                )

                if len(visible) > 1:
                    total_height += (
                        len(visible) - 1
                    ) * VERSE_GAP

                # Available lyric area.
                available_height = h - lyrics_top - 8

                while (
                    total_height > available_height
                    and len(visible) > 3
                ):
                    # Remove the furthest lyric from the group.
                    distances = [
                        abs(item["offset"])
                        for item in visible
                    ]

                    remove_index = distances.index(
                        max(distances)
                    )

                    visible.pop(remove_index)

                    total_height = sum(
                        item["height"]
                        for item in visible
                    )

                    if len(visible) > 1:
                        total_height += (
                            len(visible) - 1
                        ) * VERSE_GAP

                # Center the complete lyric group.

                group_center = (
                    lyrics_top
                    + (h - lyrics_top) / 2
                )

                top = (
                    group_center
                    - total_height / 2
                )

                # Draw each block sequentially.

                current_y = top

                for item in visible:
                    block_center = (
                        current_y
                        + item["height"] / 2
                    )

                    self._draw_lyric_block(
                        item["text"],
                        item["font"],
                        item["color"],
                        block_center,
                    )

                    current_y += (
                        item["height"]
                        + VERSE_GAP
                    )

        elif (
            lyrics_state
            and lyrics_state.get("plain")
        ):
            self._draw_center(
                "Lyrics found (not time-synced)",
                self.font_lyric_dim,
                DIM_COLOR,
                w // 2,
                center_y,
            )

        elif (
            lyrics_state
            and lyrics_state.get("found") is False
        ):
            self._draw_center(
                "No lyrics found",
                self.font_lyric_dim,
                DIM_COLOR,
                w // 2,
                center_y,
            )

        else:
            self._draw_center(
                "Loading lyrics...",
                self.font_lyric_dim,
                DIM_COLOR,
                w // 2,
                center_y,
            )

        self._present()
        
    def _draw_title(self, snapshot, x, y):

        title = snapshot.get("track_name", "")
        track_id = snapshot.get("track_id")

        if not title:
            return

        font = self.font_title

        if track_id != self._title_track_id:
            self._title_track_id = track_id
            self._title_scroll_x = 0.0
            self._title_scroll_started = (
                pygame.time.get_ticks() / 1000.0
            )

        title_surface = font.render(
            title,
            True,
            TEXT_COLOR,
        )

        available_width = (
            config.SCREEN_WIDTH - x - 8
        )

        # Short title: no marquee.
        if (
            len(title) <= TITLE_MAX_CHARS
            or title_surface.get_width() <= available_width
        ):
            self.screen.blit(
                title_surface,
                (x, y),
            )
            return

        now = pygame.time.get_ticks() / 1000.0

        elapsed = (
            now - self._title_scroll_started
        )

        title_width = title_surface.get_width()

        max_scroll = (
            title_width - available_width
        )

        scroll_duration = (
            max_scroll / TITLE_SCROLL_SPEED
        )

        cycle_duration = (
            TITLE_SCROLL_PAUSE
            + scroll_duration
            + TITLE_SCROLL_PAUSE
        )

        cycle_position = (
            elapsed % (
                cycle_duration
                + TITLE_SCROLL_PAUSE
            )
        )

        if cycle_position < TITLE_SCROLL_PAUSE:
            scroll_x = 0.0

        elif cycle_position < (
            TITLE_SCROLL_PAUSE
            + scroll_duration
        ):
            scroll_elapsed = (
                cycle_position
                - TITLE_SCROLL_PAUSE
            )

            scroll_x = (
                scroll_elapsed
                * TITLE_SCROLL_SPEED
            )

        elif cycle_position < (
            TITLE_SCROLL_PAUSE
            + scroll_duration
            + TITLE_SCROLL_PAUSE
        ):
            scroll_x = max_scroll

        else:
            scroll_x = 0.0

        clip_rect = pygame.Rect(
            x,
            y,
            available_width,
            font.get_linesize(),
        )

        old_clip = self.screen.get_clip()

        self.screen.set_clip(clip_rect)

        self.screen.blit(
            title_surface,
            (
                int(x - scroll_x),
                y,
            ),
        )

        self.screen.set_clip(old_clip)

    def pump_events(self):
        for event in pygame.event.get():

            if event.type == pygame.QUIT:
                return False

            if (
                event.type == pygame.KEYDOWN
                and event.key == pygame.K_ESCAPE
            ):
                return False

        return True