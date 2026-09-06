/*
   USBDrive - the board's internal flash, as a USB stick.

   Plug the board into a PC and a small removable drive appears. Drag files
   onto it, eject it, and the sketch reads them back.

   THE HANDOVER IS THE WHOLE POINT OF THIS EXAMPLE. The sketch and the host
   must never both have the volume mounted: FatFs caches directory and
   allocation sectors, so a host writing underneath that cache corrupts one or
   both views -- silently, with the sketch still serving files from a directory
   that no longer exists on the medium. There is no way to share a FAT volume
   between two writers without a locking protocol neither side has.

   So: onPlug gives it up, onUnplug takes it back. Every sketch that uses
   FatFSUSB needs those two callbacks or something equivalent.

   256 KB MINIMUM filesystem partition. Under PlatformIO,
   board_build.filesystem_size = 256k; in the Arduino IDE, the Filesystem Size
   menu. FatFS.begin() refuses below that and says so.

   NOTE the drive is small -- around 200 KB. That is the flash partition less
   what the wear-levelling layer reserves, and a host will say so. It is a
   place for a config file or a few readings, not a memory stick.

   Released to the public domain.
*/

#include <FatFS.h>
#include <FatFSUSB.h>

/* Set by the USB callbacks, acted on in loop().
 *
 * volatile because they are written from the USB task and read from loop().
 * The filesystem work is deliberately NOT done in the callback: mounting can
 * take milliseconds, and a USB callback is not the place to spend them. */
static volatile bool hostHasIt = false;
static volatile bool handled = true;

void onPlug(uint32_t) {
    hostHasIt = true;
    handled = false;
}

void onUnplug(uint32_t) {
    hostHasIt = false;
    handled = false;
}

/* Refuse the host while the sketch is busy. Returning false reports "not
   ready", which hosts retry. Here it simply mirrors whether we have let go. */
bool driveReady(uint32_t) {
    return hostHasIt;
}

static void listFiles() {
    Serial.println("Files on the flash volume:");
    Dir dir = FatFS.openDir("/");
    int n = 0;
    while (dir.next()) {
        Serial.printf("  %-24s %u bytes\n", dir.fileName().c_str(),
                      (unsigned)dir.fileSize());
        n++;
    }
    if (!n) {
        Serial.println("  (none yet -- copy something onto the drive)");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    if (!FatFS.begin()) {
        Serial.print("FatFS.begin() failed: ");
        Serial.println(ch32h4_fatfs_last_error_string());
        Serial.println("A 256 KB or larger filesystem partition is needed.");
        return;
    }

    FSInfo info;
    if (FatFS.info(info)) {
        Serial.printf("Flash volume: %u KB total, %u KB used\n",
                      (unsigned)(info.totalBytes / 1024),
                      (unsigned)(info.usedBytes / 1024));
    }
    listFiles();

    FatFSUSB.onPlug(onPlug);
    FatFSUSB.onUnplug(onUnplug);
    FatFSUSB.driveReady(driveReady);

    if (!FatFSUSB.begin()) {
        Serial.println("FatFSUSB.begin() failed.");
        return;
    }
    Serial.println("Ready. The drive appears when a host mounts it.");
}

void loop() {
    /* THE SIGNAL THAT ACTUALLY ARRIVES.
     *
     * onPlug and onUnplug come from SCSI commands a host is not obliged to
     * send, and Windows was measured mounting this volume, writing a file and
     * ejecting it without sending either. A write, though, is a write: if the
     * host has written sectors, this sketch's view of the filesystem is stale
     * whatever it was or was not told.
     *
     * So the callbacks are a fast path, and this is the backstop. */
    static uint32_t seenWrites = 0;
    const uint32_t writes = FatFSUSB.hostWrites();
    if (writes != seenWrites) {
        seenWrites = writes;
        if (!hostHasIt) {
            Serial.println("\nThe host wrote to the drive -- unmounting.");
            hostHasIt = true;
            handled = true;
            FatFS.end();
        }
    }

    if (!handled) {
        handled = true;
        if (hostHasIt) {
            /* Hand the volume over. Anything still open is closed by end();
               a File kept across this point would be writing to a volume the
               host is simultaneously rearranging. */
            Serial.println("\nHost took the drive -- unmounting.");
            FatFS.end();
        } else {
            Serial.println("\nHost ejected -- remounting.");
            if (FatFS.begin()) {
                listFiles();
            } else {
                Serial.print("remount failed: ");
                Serial.println(ch32h4_fatfs_last_error_string());
            }
        }
    }
    delay(10);
}
