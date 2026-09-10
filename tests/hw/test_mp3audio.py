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

    The floor is deliberately far below what is measured, and the precise
    guard lives elsewhere. Decode time varies about 14% run to run (394 ms to
    451 ms observed for the same input), while enhance mode being off costs
    only the difference between 3.7x and roughly 4.7x. A bound tight enough to
    catch that would flake on the variance, so the cause is asserted directly
    against the EHMOD bit in test_flash_enhance_mode_is_actually_on and this
    test only catches a collapse.
    """
    r = kv(mp3_board.command("decode", timeout=30))
    margin = 2000000.0 / r["decode_us"]
    print(f"\n  decode: {r['decode_us']} us for 2 s of audio "
          f"({margin:.1f}x real time, {r['decode_us'] / r['frames']:.0f} us/frame)")
    assert margin > 2.5, (
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
    assert r["sck_cfg"] == 1, (
        "the flash access clock is not HCLK/2: ACTLR=%s. HCLK/1 measures 1.7x "
        "faster and BRICKS the board -- see docs/hazards.md." % r["actlr"])


def test_icystream_strips_metadata_and_reads_the_title(mp3_board):
    """Shoutcast interleaves a metadata block every icy-metaint bytes. Leaving
    those bytes in corrupts one frame every few seconds -- an audible periodic
    glitch that looks exactly like a decoder bug."""
    r = kv(mp3_board.command("icytest", timeout=20))
    assert r["payload_ok"] == 1, "the stripped bytes did not match the payload"
    assert r["title"] == "Artist - Track", r.raw
    assert r["title_changes"] == 1, r.raw


def test_icystream_with_no_metaint_is_a_passthrough(mp3_board):
    """A stream without icy-metaint must not be altered at all."""
    r = kv(mp3_board.command("icypass", timeout=20))
    assert r["payload_ok"] == 1, r.raw
    assert r["title_changes"] == 0, r.raw


def test_the_player_delivers_every_frame_to_the_sink(mp3_board):
    """Player from a memory Stream into the counting sink.

    Same MP3 and same checksum as the decoder test, so a player that loses or
    reorders a frame shows up here rather than as a glitch someone hears
    later. This equality is the one every later network test reuses.
    """
    r = kv(mp3_board.command("play", timeout=40))
    assert r["decode_errors"] == 0, r.raw
    assert r["underruns"] == 0, "the counting sink cannot underrun"
    assert r["rate"] == 44100, r.raw
    direct = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    assert r["pcm_fnv"] == direct, (
        "the player's PCM differs from the decoder's on the same input")


def test_the_player_widens_mono_to_stereo(mp3_board):
    """AudioSink takes interleaved stereo. The test MP3 is mono, so every
    frame must arrive as two identical channels rather than at half length or
    half speed."""
    r = kv(mp3_board.command("play", timeout=40))
    assert r["sink_frames"] > 0, r.raw
    assert r["mono_widened"] == 1, "left and right differed on a mono source"


def test_volume_scales_the_samples(mp3_board):
    """setVolume() lives on the player because the writes happen inside it.

    Asserted as a ratio between two runs of the same audio rather than against
    an absolute level, so it does not depend on how loud the fixture happens
    to be.
    """
    full = kv(mp3_board.command("playvol 1.0", timeout=40))
    half = kv(mp3_board.command("playvol 0.5", timeout=40))
    assert full["peak"] > 1000, "the fixture is too quiet to measure: %s" % full.raw
    ratio = half["peak"] / full["peak"]
    print(f"\n  peak at 1.0 = {full['peak']}, at 0.5 = {half['peak']} "
          f"(ratio {ratio:.3f})")
    assert 0.45 < ratio < 0.55, (
        "half volume gave %s against %s at full, a ratio of %.3f"
        % (half["peak"], full["peak"], ratio))
    assert kv(mp3_board.command("playvol 0.0", timeout=40))["peak"] == 0, (
        "zero volume still produced signal")


def test_the_percent_volume_knob_agrees_with_the_float_one(mp3_board):
    """setVolumePercent() is what the WebRadio console command drives.

    50% and 0.5 must land on the same Q8 scale, or the two APIs would disagree
    about what half volume means. 100% must be unity, not 255/256 -- an
    off-by-one there is inaudible and would quietly make full volume not full.
    """
    pct = kv(mp3_board.command("playpct 50", timeout=40))
    flt = kv(mp3_board.command("playvol 0.5", timeout=40))
    assert pct["pct"] == 50, pct.raw
    assert pct["peak"] == flt["peak"], (
        "50%% gave a peak of %s but 0.5 gave %s" % (pct["peak"], flt["peak"]))

    full = kv(mp3_board.command("playpct 100", timeout=40))
    unity = kv(mp3_board.command("playvol 1.0", timeout=40))
    assert full["pct"] == 100, full.raw
    assert full["peak"] == unity["peak"], "100%% is not unity gain"

    assert kv(mp3_board.command("playpct 0", timeout=40))["peak"] == 0
    # Clamped rather than wrapped: 200 would be 512 in Q8 and would clip.
    assert kv(mp3_board.command("playpct 200", timeout=40))["pct"] == 100
