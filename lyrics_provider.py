# Fetches time-synced lyrics from lrclib.net

import hashlib
import json
import re

import requests

import config

TIMESTAMP_RE = re.compile(r"\[(\d{2}):(\d{2})\.(\d{2,3})\]")
USER_AGENT = "PiSpotifyLyrics/1.0 (+personal Raspberry Pi project)"

def parse_lrc(lrc_text):
    lines = []
    for raw in lrc_text.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        stamps = TIMESTAMP_RE.findall(raw)
        if not stamps:
            continue
        text = TIMESTAMP_RE.sub("", raw).strip()
        for minutes, seconds, frac in stamps:
            frac_ms = int(frac) * (10 if len(frac) == 2 else 1)
            total_ms = int(minutes) * 60000 + int(seconds) * 1000 + frac_ms
            lines.append((total_ms, text))
    lines.sort(key=lambda pair: pair[0])
    return lines


def _cache_path(track_name, artist_name, duration_sec):
    key = f"{track_name.lower()}|{artist_name.lower()}|{duration_sec}"
    digest = hashlib.sha1(key.encode("utf-8")).hexdigest()
    config.LYRICS_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    return config.LYRICS_CACHE_DIR / f"{digest}.json"


def _build_result(data):
    synced_raw = data.get("syncedLyrics")
    return {
        "synced": parse_lrc(synced_raw) if synced_raw else [],
        "plain": data.get("plainLyrics"),
        "instrumental": bool(data.get("instrumental")),
        "found": True,
    }


_NOT_FOUND = {"synced": [], "plain": None, "instrumental": False, "found": False}


def _search_fallback(track_name, artist_name):
    try:
        resp = requests.get(
            "https://lrclib.net/api/search",
            params={"q": f"{artist_name} {track_name}"},
            headers={"User-Agent": USER_AGENT},
            timeout=6,
        )
        resp.raise_for_status()
        results = resp.json()
        if results:
            return _build_result(results[0])
    except requests.RequestException as exc:
        print(f"[lyrics] lrclib search failed: {exc}")
    return _NOT_FOUND


def _fetch_from_lrclib(track_name, artist_name, album_name, duration_sec):
    params = {"track_name": track_name, "artist_name": artist_name}
    if album_name:
        params["album_name"] = album_name
    if duration_sec:
        params["duration"] = duration_sec

    try:
        resp = requests.get(
            "https://lrclib.net/api/get",
            params=params,
            headers={"User-Agent": USER_AGENT},
            timeout=6,
        )
        if resp.status_code == 200:
            return _build_result(resp.json())
        if resp.status_code == 404:
            return _search_fallback(track_name, artist_name)
    except requests.RequestException as exc:
        print(f"[lyrics] lrclib request failed: {exc}")

    return _NOT_FOUND


def fetch_lyrics(track_name, artist_name, album_name, duration_ms):
    duration_sec = round(duration_ms / 1000) if duration_ms else None
    cache_file = _cache_path(track_name, artist_name, duration_sec)
    if cache_file.exists():
        return json.loads(cache_file.read_text())

    result = _fetch_from_lrclib(track_name, artist_name, album_name, duration_sec)
    cache_file.write_text(json.dumps(result))
    return result
