import argparse
import threading

import config
from lyrics_provider import fetch_lyrics
from spotify_client import SpotifyState
from ui_gl import LyricsUI


def main():
    parser = argparse.ArgumentParser(description="Pi Spotify Lyrics Display")
    parser.add_argument(
        "--windowed", action="store_true",
        help="Run in a normal desktop window instead of the SPI framebuffer "
             "(useful for testing on desktop before deploying to the Pi)",
    )
    parser.add_argument(
        "--fbdev", default=None,
        help="Framebuffer device to render to (default: /dev/fb1)",
    )
    args = parser.parse_args()

    if not config.SPOTIFY_CLIENT_ID:
        raise SystemExit(
            "SPOTIFY_CLIENT_ID is not set.\n"
            "Copy .env.example to .env and fill in your Client ID"
        )

    spotify = SpotifyState()
    spotify.ensure_authenticated()

    lyrics_state = {}
    lyrics_lock = threading.Lock()

    def on_track_change(snapshot):
        print(f"[main] now playing: {snapshot['artist_name']} - {snapshot['track_name']}")
        result = fetch_lyrics(
            snapshot["track_name"],
            snapshot["artist_name"],
            snapshot["album_name"],
            snapshot["duration_ms"],
        )
        with lyrics_lock:
            lyrics_state.clear()
            lyrics_state.update(result)

    poll_thread = threading.Thread(
        target=spotify.run_poll_loop, args=(on_track_change,), daemon=True
    )
    poll_thread.start()

    ui = LyricsUI(windowed=args.windowed, fbdev=args.fbdev)

    running = True
    while running:
        running = ui.pump_events()
        snapshot = spotify.get_snapshot()
        with lyrics_lock:
            current_lyrics = dict(lyrics_state)
        ui.render(snapshot, current_lyrics)

    spotify.stop()


if __name__ == "__main__":
    main()
