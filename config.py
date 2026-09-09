"""Central configuration. Values can be overridden with environment
variables (or a .env file in this same folder)."""
import os
from pathlib import Path

from dotenv import load_dotenv

load_dotenv()

BASE_DIR = Path(__file__).resolve().parent

# --- Spotify ---------------------------------------------------------------
# Create an app at https://developer.spotify.com/dashboard to get a
# Client ID. This app uses the PKCE flow, so no client secret is needed.
SPOTIFY_CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID", "")
SPOTIFY_REDIRECT_URI = os.environ.get(
    "SPOTIFY_REDIRECT_URI", "http://127.0.0.1:8888/callback"
)
SPOTIFY_SCOPE = "user-read-currently-playing user-read-playback-state"
TOKEN_CACHE_PATH = str(Path.home() / ".cache" / "pi-verse" / "token.json")

# How often to ask Spotify what's currently playing. Position between polls
# is interpolated locally, so this can stay low without hurting sync quality.
POLL_INTERVAL_SEC = 2.0

# --- Lyrics ------------------------------------------------------------
LYRICS_CACHE_DIR = Path.home() / ".cache" / "pi-verse" / "lyrics"

# --- Display -----------------------------------------------------------
# Match this to your actual framebuffer resolution. Check with:
#   fbset -fb /dev/fb1
SCREEN_WIDTH = int(os.environ.get("SCREEN_WIDTH", 480))
SCREEN_HEIGHT = int(os.environ.get("SCREEN_HEIGHT", 320))

# Optional: point this at a .ttf with good Unicode coverage (accents,
# non-Latin lyrics) for nicer rendering. Falls back to a system font.
FONT_PATH = str(BASE_DIR / "assets" / "Noto_Sans" /  "NotoSans.ttf")
