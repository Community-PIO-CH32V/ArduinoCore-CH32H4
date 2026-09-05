"""setup1()/loop1() on the V3F, and the channels between the two cores.

This file costs a second flash, because the dual-core sketch is a different
image. It is worth it. Everything here was written blind and shipped broken:

  * The V3F's vector table was in flash, where a mode-3 vector read returns
    garbage, with 60 entries pointing at address 0 -- which is _start_v3f. Both
    were harmless for as long as that core only woke the V5F and slept, so no
    single-core test could see either.
  * mutexTryLock() was inverted, because the SDK's HSEM_FastTake() reports
    success exactly when the take was a no-op.
  * A call to address 0 in the V5F's loop() re-ran the V3F's startup underneath
    a running system, which presented as a dual-core fault and was not one.

See docs/hazards.md for each.
"""


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


def test_both_cores_start(dualcore_board):
    """The V3F must reach setup1(), and say so, without the board resetting.

    The failure this replaces printed this exact line and then lockup-reset
    about 25 times a second, so the presence of the line is not enough on its
    own -- the prompt after it is what says the board is still there."""
    banner = dualcore_board.banner
    assert "V3F: running setup1/loop1" in banner, banner
    assert "dualcore ready" in banner, banner


def test_sketch_runs_on_the_fast_core(dualcore_board):
    """setup()/loop() belong to the V5F at 400 MHz, not the V3F at 100."""
    d = _kv(dualcore_board.command("coreinfo"))
    assert d["core0_num"] == "1", d
    assert d["core0_hz"] == "400000000", d
    assert d["bus_hz"] == "100000000", d


def test_the_second_core_is_really_running(dualcore_board):
    """loop1() increments a counter. If it moves, the V3F is executing the
    sketch's own code -- not merely awake, and not merely having printed."""
    d = _kv(dualcore_board.command("core1alive", timeout=5.0))
    assert d["core1_iterations_moved"] == "1", d
    # 50 ms of a 100 MHz core doing almost nothing. Hundreds at the very least;
    # the bound is loose because the number is not the point, movement is.
    assert int(d["core1_delta"]) > 100, d


def test_fifo_round_trips_between_the_cores(dualcore_board):
    """Core 0 pushes, core 1 pops, inverts and pushes back.

    Proves both directions. A ring that works only one way still passes a test
    that just checks the pop, because the value would come from this core's own
    push sitting in the wrong queue."""
    d = _kv(dualcore_board.command("fifotest", timeout=5.0))
    assert d["fifo_roundtrip"] == "ok", d
    assert d["fifo_got"] == "0xEDCBA987", d      # 0x12345678 ^ 0xFFFFFFFF
    assert int(d["core1_echoed"]) >= 1, d


def test_fifo_reports_full_before_it_wraps(dualcore_board):
    """A ring that distinguishes full from empty by index equality gives up one
    slot. Seven of eight is correct; eight would mean a full ring reads as an
    empty one and the next pop returns stale data."""
    d = _kv(dualcore_board.command("fifodepth"))
    assert d["fifo_pushed_before_full"] == "7", d


def test_hardware_semaphore_is_exclusive(dualcore_board):
    """Take, take again, release, take.

    The middle one must fail: HSEM records the owning core, so a second take is
    not a re-entrant lock. This is the test that catches an inverted take --
    the SDK's HSEM_FastTake() returns success exactly when the semaphore was
    ALREADY held, which reads as 0/1/0 here instead of 1/0/1 and makes every
    lock built on it silently wrong. The console lock is built on it."""
    d = _kv(dualcore_board.command("mutextest"))
    assert d["mutex_first"] == "1", ("the first take must succeed", d)
    assert d["mutex_second"] == "0", ("a second take must not", d)
    assert d["mutex_after_unlock"] == "1", ("and it must work again after", d)


def test_semaphore_take_returns_the_previous_state(dualcore_board):
    """The raw register behaviour the take is built on.

    Reading RLRX is the take, and it returns the state BEFORE it -- so zero is
    what success looks like. This is here because the SDK helper documents the
    opposite ("READY - Take success") and the values are the only thing that
    settles it."""
    d = _kv(dualcore_board.command("hsemraw"))
    assert d["coreid"] == "1", d
    assert d["r1"] == "0x0", ("a take from free must report the free state", d)
    assert d["r2"] == "0x80000100", ("a second take reports us as owner", d)
    assert d["rx"] == "0x80000100", ("and a plain read agrees", d)
    assert d["r3"] == "0x0", ("after release, free again", d)


def test_the_board_does_not_reset_while_running(dualcore_board):
    """Both cores must keep running, not reboot in a loop that looks like it.

    This is the test the whole file exists for. The failure it replaces booted
    perfectly, printed every expected line, answered no commands, and reset
    about 25 times a second -- so every assertion above would have passed on
    the banner of a boot that was about to die. Watching for a second boot
    banner is what tells the two apart.
    """
    import time
    ser = dualcore_board.ser
    ser.reset_input_buffer()

    seen = ""
    deadline = time.time() + 4.0
    while time.time() < deadline:
        # Keep the board working rather than idling: the fault was in loop(),
        # and an idle loop() is not the same code path as a busy one.
        ser.write(b"coreinfo\n")
        ser.flush()
        time.sleep(0.25)
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            seen += chunk.decode(errors="replace")

    assert "CH32H4 Arduino core" not in seen, \
        "the board rebooted while under test:\n" + seen[-1500:]
    assert seen.count("core0_num=1") >= 4, \
        "the board stopped answering:\n" + seen[-1500:]


def test_the_previous_run_ended_cleanly(dualcore_board):
    """A deliberate reset, then read what the V3F says about the last run.

    A lockup leaves no fault record -- the handler never runs -- so the reset
    cause is the only trace, and it is printed before anything else. A fault
    that did reach the handler is replayed as a "v5f fault:" or "v3f trap:"
    line. Neither may appear.
    """
    report = dualcore_board.reboot()
    assert "rst=" in report, report
    assert "lockup" not in report, ("the previous run lockup-reset", report)
    assert "v5f fault:" not in report, report
    assert "v3f trap:" not in report, report
    assert "NOT waking it" not in report, \
        ("the V3F refused to start the V5F, which means "
         "CH32H4_FAULT_REBOOT_LIMIT crash-reboots in a row", report)


def test_the_mutex_is_recursive_within_one_core(dualcore_board):
    """CH32H4Mutex counts recursion; the raw HSEM underneath cannot.

    HSEM records the taking core as the owner and refuses a second take from
    it, so a lock() inside a lock() on the bare semaphore spins until the
    watchdog. The depth counter in CH32H4Mutex is what makes the nested case
    return instead -- and what makes CH32H4MutexGuard usable inside a function
    whose caller already holds the lock, which is most of why a scope guard is
    worth having.
    """
    d = _kv(dualcore_board.command("mutexrecurse"))
    assert d["rec_valid"] == "1", d
    # Allocated from the user range, never the core's 0-3.
    assert int(d["rec_id"]) >= 4, d
    assert d["rec_inner"] == "1", ("a nested tryLock() must succeed -- the "
                                   "depth count is what makes it", d)
    assert d["rec_after"] == "1", ("after as many unlocks as locks it must be "
                                   "free again", d)


def test_the_mutex_actually_excludes_the_other_core(dualcore_board):
    """Both cores stamping one sixteen-word object, for a second and a half.

    Every writer puts the same value in all sixteen words, and every reader
    checks they agree. A reader that sees two different values caught the
    other core mid-write. Under the lock that must never happen.
    """
    d = _kv(dualcore_board.command("xmutex on", timeout=8.0))
    assert d["x_locked"] == "1", d
    # Both cores must actually have run, or the test proved nothing.
    assert int(d["x_v5f_writes"]) > 100, d
    assert int(d["x_v3f_writes"]) > 100, d
    assert d["x_tears"] == "0", ("the lock let a torn write through", d)


def test_without_the_mutex_the_same_test_fails(dualcore_board):
    """The negative half, and the reason the positive one means anything.

    The identical loop with the lock taken out must tear. If it does not, the
    test is not sensitive enough to detect a mutex that does nothing -- and a
    mutex that does nothing passes the test above perfectly.
    """
    d = _kv(dualcore_board.command("xmutex off", timeout=8.0))
    assert d["x_locked"] == "0", d
    assert int(d["x_v5f_writes"]) > 100, d
    assert int(d["x_v3f_writes"]) > 100, d
    assert int(d["x_tears"]) > 0, (
        "two cores wrote one object with no lock and nothing tore -- this "
        "test can no longer tell a working mutex from an absent one", d)


def test_flash_writes_are_safe_while_the_other_core_runs(dualcore_board):
    """The one that used to hang the board, and cost a bench rescue.

    A page program does not complete while the V3F is fetching from the array
    being written -- it has no instruction cache, unlike the V5F, so every
    instruction it executes comes from the flash the other core is busy with.
    Erasing is fine; programming is not.

    The flash driver now parks the other core for the duration, through an IPC
    interrupt whose handler spins in ITCM. `fs_parked=0` below says the SKETCH
    did not park anything -- if this passes, the driver did it unasked, which
    is the whole point: LittleFS and EEPROM get it without knowing it exists.
    """
    d = _kv(dualcore_board.command("flash write1", timeout=20.0))
    assert d["fs_parked"] == "0", ("the sketch should not be parking here -- "
                                   "the driver should", d)
    assert d["fs_ok"] == "1", d
    assert d["fs_verify_bad"] == "0", d
    assert d["fs_v3f_fault"] == "0x0", d


def test_a_large_flash_write_is_safe_while_the_other_core_runs(dualcore_board):
    """128 KB of it -- an OTA-sized image, erased, programmed and read back,
    with loop1() running throughout. Sixteen page erases and 512 page
    programs, any one of which used to be enough to hang the part."""
    d = _kv(dualcore_board.command("flash big", timeout=30.0))
    assert d["fs_parked"] == "0", d
    assert d["fs_ok"] == "1", d
    assert d["fs_verify_bad"] == "0", d
    assert d["fs_v3f_fault"] == "0x0", d


def test_the_other_core_survives_the_flash_write(dualcore_board):
    """Parked is not the same as dead. The V3F has to come back and keep
    running loop1() afterwards, or the fix has traded a hang for a stall."""
    dualcore_board.command("flash big", timeout=30.0)
    d = _kv(dualcore_board.command("core1alive", timeout=6.0))
    assert d["core1_iterations_moved"] == "1", d
    assert int(d["core1_delta"]) > 100, d


def test_the_ota_committer_writes_a_staged_image(dualcore_board):
    """The last step of an over-the-air update, rehearsed on spare flash.

    ch32h4_ota_commit() is the piece that erases and reprograms the flash it is
    stored in, so it runs entirely from ITCM and calls nothing outside itself
    -- not memcpy, not the SDK's erase, not the flash driver. It parks the
    other core and then does not return: it resets the part.

    Everything here is the real path except the destination, which is 352 KB
    into the sketch area rather than at its start. Erasing the code that called
    it is the one thing this cannot rehearse.

    The image is 32 KB of a pattern, staged in RAM. After the reset the region
    has to match it exactly -- a committer that programs the first page and
    stops looks identical from the outside until you read the rest back, which
    is how the first version of this was caught.
    """
    dualcore_board.command("otacommit", timeout=20.0)
    # It reset itself; wait for the prompt to come back.
    dualcore_board.reboot()

    d = _kv(dualcore_board.command("otaverify", timeout=15.0))
    assert d["ota_bad"] == "0", ("the committed image does not match what was "
                                 "staged", d)

    alive = _kv(dualcore_board.command("core1alive", timeout=6.0))
    assert alive["core1_iterations_moved"] == "1", alive
