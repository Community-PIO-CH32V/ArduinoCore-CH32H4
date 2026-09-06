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

   So the sketch gives the volume up when the host takes it, and takes it back
   afterwards. WATCH hostChanged() TO DO THAT, not the onPlug/onUnplug
   callbacks alone: those come from SCSI commands a host is not obliged to
   send, and Windows was measured mounting this volume, writing a file and
   ejecting it without sending either. hostChanged() also goes true on the
   first host write, which no host can perform silently.

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

/* Set from the USB callbacks, which fire only on hosts that send the SCSI
 * commands behind them -- Windows, measured, does not. They are a refinement;
 * hostChanged() in loop() is what actually carries the contract.
 *
 * volatile because they are written from the USB task and read from loop().
 * The filesystem work is deliberately NOT done in the callback: mounting takes
 * milliseconds, and a USB callback is not the place to spend them. */
static volatile bool hostHasIt = false;
static bool weGaveItUp = false;

void onPlug(uint32_t) {
    hostHasIt = true;
}

void onUnplug(uint32_t) {
    hostHasIt = false;
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
    /* ONE THING TO WATCH.
     *
     * hostChanged() goes true when the host mounts the volume (where the host
     * announces it) or writes to it (which it cannot do silently). Windows
     * does neither announcement -- it was measured mounting, writing and
     * ejecting without sending either SCSI command -- so a sketch that waited
     * only for onPlug would go on serving files from a directory the host had
     * already rewritten, and would not find out until something looked wrong.
     */
    if (FatFSUSB.hostChanged() && !weGaveItUp) {
        FatFSUSB.clearHostChanged();
        weGaveItUp = true;
        Serial.println("\nThe host has the drive -- unmounting.");
        /* end() closes everything. A File kept open across this point would be
           writing into a volume the host is rearranging underneath it. */
        FatFS.end();
    }

    /* Taking it back is the sketch's decision, because nothing reliably says
       the host has finished. onUnplug fires where a host sends it; otherwise a
       sketch picks its own moment -- a button, an idle timeout, or as here a
       quiet spell with no further host writes. */
    if (weGaveItUp && !hostHasIt) {
        static uint32_t quietSince = 0;
        const uint32_t writes = FatFSUSB.hostWrites();
        static uint32_t lastWrites = 0;
        if (writes != lastWrites) {
            lastWrites = writes;
            quietSince = millis();
        } else if (quietSince && millis() - quietSince > 3000) {
            quietSince = 0;
            weGaveItUp = false;
            FatFSUSB.clearHostChanged();
            Serial.println("\nHost has been quiet -- remounting.");
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
