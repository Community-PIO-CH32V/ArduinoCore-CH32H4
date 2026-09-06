/* FatFSUSB, on Serial1 so the USB side is free for mass storage.
 *
 * The interesting half of this cannot be automated: whether a real host
 * enumerates the drive, mounts it, and copies files both ways. What CAN be
 * checked from here is everything up to that -- that the volume exists, that
 * begin() presents the right block count, and that the block callbacks serve
 * the same data FatFs sees.
 */
#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>

/* NO WATCHDOG HERE, deliberately.
 *
 * An earlier version armed the IWDG so a hang would reset the board instead of
 * wedging the debug probe. It reset the board in the middle of a 120 ms
 * FatFS.begin() instead -- at reload 780 AND at 4095 -- which means the timeout
 * this part's IWDG actually gives is nothing like the 5 s and 26 s that
 * 40 kHz / 256 predicts. Whatever it is has not been measured, and a watchdog
 * whose period you have not measured is a random reset generator: it cost two
 * bench rescues here and sent a working filesystem to be debugged as a hang.
 *
 * Measure it before using it. Until then the step markers below are the
 * diagnosis, and they cost nothing. */

/* Step markers, flushed, so a hang names the operation that caused it rather
   than just the command. */
static void mark(const char *what) {
  Serial1.print("step="); Serial1.println(what);
  Serial1.flush();
}

static volatile bool hostHasIt = false;
static volatile uint32_t plugs = 0, unplugs = 0;

void onPlug(uint32_t)   { hostHasIt = true;  plugs++; }
void onUnplug(uint32_t) { hostHasIt = false; unplugs++; }

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("usbdrive starting");

  /* In setup(), before enumeration finishes -- which is the ordering a real
     sketch uses and the one that does not cost a re-enumeration. The serial
     commands below can still drive it late, which exercises the other path. */
  mark("ftl_ctor");
  ch32h4_fatfs_ftl();
  mark("fs_begin_call");
  bool fs = FatFS.begin();
  mark("fs_begin_done");
  Serial1.print("boot_fs_mount="); Serial1.println(fs ? 1 : 0);
  Serial1.print("boot_fs_err=");
  Serial1.println(ch32h4_fatfs_last_error_string());
  if (fs) {
    FatFSUSB.onPlug(onPlug);
    FatFSUSB.onUnplug(onUnplug);
    mark("usb_begin_call");
    bool u = FatFSUSB.begin();
    mark("usb_begin_done");
    Serial1.print("boot_usb_begin="); Serial1.println(u ? 1 : 0);
  }
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "fsbegin") {
          bool ok = FatFS.begin();
          Serial1.print("fs_mount="); Serial1.println(ok ? 1 : 0);
          Serial1.print("fs_err=");
          Serial1.println(ch32h4_fatfs_last_error_string());
        } else if (line == "fsformat") {
          mark("ftl_create");
          ch32h4_fatfs_ftl();
          mark("fs_format_call");
          bool ok = FatFS.format();
          mark("fs_format_done");
          Serial1.print("fs_format="); Serial1.println(ok ? 1 : 0);
        } else if (line == "usbbegin") {
          mark("usb_begin_call");
          FatFSUSB.onPlug(onPlug);
          FatFSUSB.onUnplug(onUnplug);
          bool ok = FatFSUSB.begin();
          Serial1.print("usb_begin="); Serial1.println(ok ? 1 : 0);
          Serial1.print("usb_started=");
          Serial1.println(FatFSUSB.started() ? 1 : 0);
        } else if (line == "usbinfo") {
          Serial1.print("usb_blocks="); Serial1.println(ch32h4_fatfs_lba_count());
          Serial1.print("usb_plugs="); Serial1.println(plugs);
          Serial1.print("usb_unplugs="); Serial1.println(unplugs);
          Serial1.print("usb_host_has_it="); Serial1.println(hostHasIt ? 1 : 0);
          Serial1.print("usb_worst_write_us=");
          Serial1.println(FatFSUSB.worstWriteMicros());
          Serial1.print("usb_host_writes=");
          Serial1.println(FatFSUSB.hostWrites());
          extern volatile uint32_t ch32h4_fatfsusb_prevent_calls;
          Serial1.print("usb_prevent_calls=");
          Serial1.println((uint32_t)ch32h4_fatfsusb_prevent_calls);
        } else if (line == "usbend") {
          FatFSUSB.end();
          Serial1.print("usb_started=");
          Serial1.println(FatFSUSB.started() ? 1 : 0);
        } else if (line == "blockcheck") {
          /* The MSC read path must return exactly what the filesystem's own
             block layer holds -- they are the same layer, and this proves the
             callback is wired to it rather than to a copy. */
          uint8_t viaMsc[512], viaFtl[512];
          int32_t n = FatFSUSB.read10(0, viaMsc, 512);
          bool ok = ch32h4_fatfs_lba_read(0, viaFtl);
          Serial1.print("block_read="); Serial1.println((int)n);
          Serial1.print("block_same=");
          Serial1.println(ok && n == 512 && !memcmp(viaMsc, viaFtl, 512) ? 1 : 0);
          /* A FAT volume starts with a jump instruction and ends with 55 AA. */
          Serial1.print("block_bootsig=");
          Serial1.println((viaMsc[510] == 0x55 && viaMsc[511] == 0xAA) ? 1 : 0);
        } else {
          Serial1.print("unknown: "); Serial1.println(line);
        }
        line = "";
      }
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
