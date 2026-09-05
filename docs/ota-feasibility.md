# ArduinoOTA on the CH32H41x: what it would take

A feasibility study, not a plan. The question was whether this core can support
`ArduinoOTA` — update the sketch over Ethernet, the way the ESP cores and
arduino-pico do — and the answer is **yes, with one limitation that is
structural and one unknown that has to be measured on hardware before anything
is designed around it.**

Everything below is grounded in what is already in the tree; the numbers come
from builds and from the linker script, not from the datasheet.

---

## What already exists

Most of the hard part is done, which is why this is worth writing up rather
than dismissing.

**Self-programming works, from RAM.** `cores/ch32h4/ch32h4_flash.c` has the
erase and program sequence, and `flash_program_page()` is already
`__itcm_func` — it executes from ITCM with interrupts masked, which is what
makes it safe to write flash while the core is otherwise executing XIP from
that same flash. EEPROM emulation and LittleFS both go through it today, so it
is exercised on every run of the hardware suite.

The constants that matter and are easy to get wrong are already handled:
erased flash reads `0xE339E339`, not `0xFFFFFFFF`; the erase page is 8 KB on
this part and 4 KB on the 480 KB one, read at run time.

**The network is there.** lwIP 2.2.1 with TCP, UDP and DNS, and
`EthernetServer` / `EthernetUDP` over it. The ArduinoOTA protocol needs exactly
those two sockets and nothing else.

**MD5 is there.** `cores/ch32h4/MD5Builder.{h,cpp}`, added for `WebServer`.
The ArduinoOTA handshake is an MD5 of the image and, when a password is set, an
MD5-based challenge; both are covered.

**The library is portable.** arduino-pico's `ArduinoOTA` is LGPL and written
against `WiFiUDP`/`WiFiClient`. `WebServer` and `HTTPClient` came across by
replacing those with `arduino::Client` and `UDP`, and this would be the same
substitution — the transport is used through the interface in both.

---

## The limitation: there is nowhere to put a second copy

This is the part that does not go away with effort.

The flash user area is 960 KB, laid out like this:

```
0x08000000  the V3F stub                32 KB
0x08008000  the sketch                  912 KB - filesystem_size
            LittleFS                    filesystem_size (may be 0)
0x080EC000  EEPROM                      16 KB
0x080F0000  end of user flash
```

The ESP cores and arduino-pico can do OTA safely because they have room for the
new image and the old one at once: download to the spare slot, verify it, and
only then switch. **A 912 KB sketch region in a 960 KB flash has no spare
slot**, and creating one means halving the maximum sketch to about 450 KB —
for every sketch, whether or not it ever does an update.

So the realistic shape is: stage the image somewhere that is not the sketch
region, verify it completely, and then commit it in one pass that must not be
interrupted.

### Staging in RAM is the option that fits

The `SHARED` region is 459 KB and is the second half of the heap — `_sbrk`
starts in DTCM and spills into `SHARED`, so a single large `malloc()` lands
there without any special allocator. Most sketches use almost none of it:
`.pio` builds report `SHARED` at 0% for everything in `tests/sketches` except
the networking images.

That gives a practical ceiling of roughly **400 KB for an over-the-air image**,
against a 912 KB ceiling for one flashed by probe. That is enough for most
sketches and not enough for all of them: `tlsserver`, the HTTPS server test
image, is 385 KB of flash today and would be inside the limit only just.

The alternative, streaming straight into the sketch region as the bytes
arrive, removes the size limit and is strictly worse: a download that stops
half way leaves a board that does not boot, and there is no verification step
possible because the thing being verified has already overwritten the thing
that would do the verifying.

### And the commit is a window where power loss bricks the board

Between the first erase and the last program — some hundreds of milliseconds
for a 400 KB image — the sketch region is neither the old image nor the new
one. Losing power there means a board that needs a probe and `wlink erase` to
recover.

This is not a bug to be fixed; it is what a single-slot layout costs. It should
be **said plainly in the library's documentation** rather than discovered. It
is also an argument for keeping the staged-and-verified design over the
streaming one: the window is as short as it can be, and it never opens for an
image that was truncated or corrupted in transit.

---

## The unknown, now measured

**What happens to the V3F while the V5F is programming flash?** It hangs the
part — and the answer arrived with a bench rescue, so it is worth stating
plainly.

Of the three possibilities this section used to list, none was right. There is
no stall-and-resume, no bank separation and no garbage execution. The page
program simply never returns, and the watchdog is the only way back.

| operation | V3F parked in ITCM | V3F running from flash |
|---|---|---|
| erase one 8 KB page | ok | ok |
| erase + program one 256 B page | ok | **hangs** |
| erase + program 128 KB | ok, verified | hangs |

The asymmetry is the cache. The V5F has an instruction cache and its
programming loop is in ITCM besides; the V3F is in-order with no cache, so it
fetches every instruction from the array being written. `docs/hazards.md` has
the full write-up and the evidence.

**This is a live hazard, not only an OTA constraint.** A dualcore sketch
writing LittleFS or EEPROM while `loop1()` runs hangs today. Every test that
made flash writes look safe was run with the second core asleep in stop mode,
where it fetches nothing.

**So the committer must park the V3F**, and so must the filesystem. The
demonstration in `tests/sketches/dualcore` is a request flag and an
acknowledgement in `.xcore` plus an `__itcm_func` spin loop; with it, 128 KB
erases and programs cleanly while the V3F spins two million times. A
cooperative flag is not enough on its own, because `loop1()` need not ever
reach the check -- the general mechanism is an ITCM-resident IPC interrupt
handler, called from `ch32h4_flash_erase()` and `ch32h4_flash_write()` so that
nothing has to remember to do it.

## What is missing beyond the library

**mDNS, if the board should appear in the IDE's port list.** lwIP ships an
mDNS responder in `system/lwip/src/apps/mdns/`; it is in the tree and not in
the build. Without it, `espota.py` and `arduino-cli upload --port <address>`
still work — you type the address instead of picking it from a menu. With it,
the board shows up as a network port the way an ESP32 does. This is the
difference between "OTA works" and "OTA is discoverable", and it is independent
of everything above.

**A network upload recipe in `platform.txt`.** There is none today; uploads go
through `wlink`. The Arduino side of this is `tools.<name>.upload.network_pattern`
and a `upload.protocol` of `network`, plus shipping `espota.py` or equivalent
as a tool in the index.

**A build option.** OTA means an always-listening UDP socket, a TCP server, an
MD5 implementation and a few hundred kilobytes of staging heap held in reserve.
That should be `board_build.ota = enabled` and off by default, like everything
else of that size in this core.

---

## Recommendation

Feasible, and worth doing, in this order:

1. ~~**Measure the V3F behaviour during a large flash write.**~~ Done: it
   hangs the part, and the fix is to park the V3F in ITCM. See above. The same
   fix is owed to LittleFS and EEPROM, which have the same problem today.
2. **Port arduino-pico's `ArduinoOTA` onto `arduino::Client`/`UDP`**, as
   `WebServer` and `HTTPClient` were. Stage in heap, verify MD5 before
   committing, commit from ITCM with the V3F parked if step 1 says so.
3. **Document the two limits in the header**, where someone will read them: an
   image ceiling of roughly 400 KB, and a commit window in which power loss
   costs a probe.
4. **mDNS, separately**, as its own change. It is useful on its own — a board
   findable by name is worth having whether or not it can be updated over the
   network — and bundling it here makes both harder to review.

What would change the calculus: a part with more flash, or a willingness to
halve the maximum sketch size in exchange for a genuinely fail-safe two-slot
update. Neither is this core's call to make on its own.
