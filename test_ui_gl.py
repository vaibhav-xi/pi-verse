import sys
import types
import ctypes
from unittest.mock import MagicMock, patch

sys.path.insert(0, ".")

import ui_gl  # noqa: E402


def make_ui_with_mock_lib():
    mock_lib = MagicMock()
    mock_lib.pv_init.return_value = 0
    mock_lib.pv_set_album_art.return_value = 0

    with patch.object(ui_gl, "_load_library", return_value=mock_lib):
        ui = ui_gl.LyricsUI()
    return ui, mock_lib


failures = []


def check(cond, msg):
    if not cond:
        print(f"  FAIL: {msg}")
        failures.append(msg)
    else:
        print(f"  ok:   {msg}")


print("=== pv_init is called once with the right font path ===")
ui, mock_lib = make_ui_with_mock_lib()
check(mock_lib.pv_init.call_count == 1, "pv_init called exactly once")
args = mock_lib.pv_init.call_args[0]
check(args[2] == ui_gl.config.FONT_PATH.encode(), "font path matches config.FONT_PATH")

print("=== render() with no track calls pv_render_frame(has_track=False, ...) ===")
ui.render({}, {})
call_args = mock_lib.pv_render_frame.call_args[0]
check(call_args[0] is False, "has_track is False")
check(call_args[1] is None, "track_id is None")
check(mock_lib.pv_set_album_art.call_count == 0, "no album art fetch attempted for no-track frame")

print("=== render() with a real track marshals fields correctly ===")
snapshot = {
    "track_id": "abc123",
    "track_name": "Test Song",
    "artist_name": "Test Artist",
    "duration_ms": 200000,
    "progress_ms": 15000,
    "is_playing": True,
    "album_art_url": None,
}
lyrics = {
    "synced": [(1000, "first line"), (5000, "second line")],
    "plain": None,
    "instrumental": False,
    "found": True,
}
with patch.object(ui_gl, "requests") as mock_requests:
    ui.render(snapshot, lyrics)
call_args = mock_lib.pv_render_frame.call_args[0]
check(call_args[0] is True, "has_track is True")
check(call_args[1] == b"abc123", "track_id encoded correctly")
check(call_args[2] == b"Test Song", "track_name encoded correctly")
check(call_args[4] == 200000, "duration_ms passed through")
check(call_args[5] == 15000, "progress_ms passed through")
synced_array = call_args[7]
synced_count = call_args[8]
check(synced_count == 2, "synced_count matches list length")
check(synced_array[0].timestamp_ms == 1000, "first synced line timestamp correct")
check(synced_array[0].text == b"first line", "first synced line text correct")
check(synced_array[1].timestamp_ms == 5000, "second synced line timestamp correct")

print("=== render() with empty lyrics_state ({}) sets has_lyrics_data=False ===")
with patch.object(ui_gl, "requests"):
    ui.render(snapshot, {})
call_args = mock_lib.pv_render_frame.call_args[0]
check(call_args[10] is False, "has_lyrics_data is False for empty dict (still loading)")
check(call_args[7] is None, "synced_array is None when there are no synced lines")
check(call_args[8] == 0, "synced_count is 0")

print("=== album art: same URL is not re-fetched ===")
mock_lib.reset_mock()
snap_with_art = dict(snapshot, album_art_url="http://example.com/art.jpg")
with patch.object(ui_gl, "requests") as mock_requests:
    mock_requests.get.return_value.content = b"fake-jpeg-bytes"
    mock_requests.get.return_value.raise_for_status = MagicMock()
    ui.render(snap_with_art, lyrics)
    check(mock_requests.get.call_count == 1, "first render with new art URL fetches once")
    ui.render(snap_with_art, lyrics)
    check(mock_requests.get.call_count == 1, "second render with SAME art URL does not re-fetch")
check(mock_lib.pv_set_album_art.call_count == 1, "pv_set_album_art called once (cached after)")

print("=== album art: changing URL triggers a new fetch ===")
with patch.object(ui_gl, "requests") as mock_requests:
    mock_requests.get.return_value.content = b"different-bytes"
    mock_requests.get.return_value.raise_for_status = MagicMock()
    snap_new_art = dict(snapshot, album_art_url="http://example.com/other.jpg")
    ui.render(snap_new_art, lyrics)
    check(mock_requests.get.call_count == 1, "new URL triggers exactly one fetch")

print("=== album art: None URL clears art instead of fetching ===")
mock_lib.reset_mock()
snap_no_art = dict(snapshot, album_art_url=None)
with patch.object(ui_gl, "requests") as mock_requests:
    ui.render(snap_no_art, lyrics)
    check(mock_requests.get.call_count == 0, "no fetch attempted for None URL")
check(mock_lib.pv_clear_album_art.call_count == 1, "pv_clear_album_art called for None URL")

print("=== _encode handles None safely ===")
check(ui_gl._encode(None) is None, "_encode(None) is None")
check(ui_gl._encode("hello") == b"hello", "_encode('hello') == b'hello'")
check(ui_gl._encode("café") == "café".encode("utf-8"), "_encode handles non-ASCII (UTF-8)")

print("=== close() calls pv_shutdown exactly once, even if called twice ===")
ui.close()
ui.close()
check(mock_lib.pv_shutdown.call_count == 1, "pv_shutdown called exactly once despite double close()")

print()
print("ALL TESTS PASSED" if not failures else f"{len(failures)} TEST(S) FAILED")
sys.exit(1 if failures else 0)
