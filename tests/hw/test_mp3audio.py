"""The MP3 decoder, IcyStream and the player, none of which need a network.

Everything here is SILENT. The sketch's sink counts and checksums frames
instead of driving I2S, because there is a real amplifier on I2S1's pins.
"""
import pathlib

import pytest

from conftest import kv

DATA = pathlib.Path(__file__).resolve().parents[1] / "data" / "tone.mp3"


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def test_the_embedded_mp3_matches_the_file_on_disk(mp3_board):
    """The board's copy and the pytest server's copy must be the same bytes.

    They are generated from one file by tools/mp3_to_header.py, and this is
    what catches someone regenerating one and not the other -- which would
    make the network test compare two different recordings and still pass.
    """
    r = kv(mp3_board.command("mp3info", timeout=20))
    assert r["mp3_len"] == DATA.stat().st_size, r.raw
    assert r["mp3_fnv"] == fnv1a(DATA.read_bytes()), r.raw


def test_the_decoder_reports_the_stream_format(mp3_board):
    r = kv(mp3_board.command("decode", timeout=30))
    assert r["rate"] == 44100, r.raw
    assert r["channels"] == 1, r.raw
    assert r["errors"] == 0, "a clean MP3 should decode without a resync"


def test_the_decoder_produces_the_expected_frame_count(mp3_board):
    """2 s at 44.1 kHz in 1152-sample frames is about 77 frames, and a decoder
    that silently drops or duplicates one is otherwise invisible."""
    r = kv(mp3_board.command("decode", timeout=30))
    assert 74 <= r["frames"] <= 80, r.raw


def test_decoding_is_deterministic(mp3_board):
    """The same bytes must decode to the same PCM every time.

    This checksum is the value every later test compares against -- the
    player, the HTTP path and the TLS path all assert equality with it -- so
    it has to be stable before any of them is worth anything.
    """
    first = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    second = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    assert first == second, "decoding the same buffer twice differed"


def test_decoding_keeps_ahead_of_real_time(mp3_board):
    """Decode must be faster than playback, with margin to spare.

    Asserted rather than merely reported, because a build-flag change or a
    decoder edit that halves throughput does not fail anything else -- the
    radio just starts stuttering, on someone else's bench, later.

    The bound is deliberately loose. The measured margin is about 3.6x on this
    fixture (mono, 64 kbps); a real station is stereo at 128 kbps and roughly
    twice the work, so the useful question is whether the number has moved,
    not whether it clears some absolute bar.
    """
    r = kv(mp3_board.command("decode", timeout=30))
    margin = 2000000.0 / r["decode_us"]
    print(f"\n  decode: {r['decode_us']} us for 2 s of audio "
          f"({margin:.1f}x real time, {r['decode_us'] / r['frames']:.0f} us/frame)")
    assert margin > 2.0, (
        "decoding 2 s of mono 64 kbps took %d us (%.1fx real time). Stereo at "
        "128 kbps is roughly twice this work, so anything near 2x will not "
        "sustain a real stream." % (r["decode_us"], margin))


def test_flash_enhance_mode_is_actually_on(mp3_board):
    """The core enables flash enhance mode, and for a long time that failed.

    FLASH->ACTLR is write-protected while the flash is locked, so the call
    returned having changed nothing -- silently, because it returns void and
    the register accepts the write. It is worth 32% of decode throughput, so
    it is asserted here against the hardware's own ENHANCE_STATUS bit rather
    than trusted.
    """
    r = kv(mp3_board.command("actlr", timeout=20))
    assert r["ehmod"] == 1, (
        "EHMOD is clear: flash enhance mode did not take. Is FLASH_Unlock() "
        "still called before it in main_v5f.c? ACTLR=%s" % r["actlr"])
    assert r["enhance_status"] == 1, (
        "EHMOD is set but the hardware did not confirm it: ACTLR=%s" % r["actlr"])
