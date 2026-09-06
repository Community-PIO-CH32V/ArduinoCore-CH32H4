/*
   ListFiles - a FAT filesystem on the chip's own flash.

   Writes a file if there isn't one, then lists the directory and reports how
   full the volume is. Reset the board and the file is still there -- that
   being the entire point of a filesystem.

   256 KB MINIMUM filesystem partition. Under PlatformIO,
   board_build.filesystem_size = 256k; in the Arduino IDE, the Filesystem Size
   menu. Below that the wear-levelling layer's fixed reserve leaves under
   90 KB, which is a legal but unusual FAT12 volume and a coin-toss on a
   Windows host over USB, so begin() refuses and says which setting to change.

   THIS IS THE SAME PARTITION LITTLEFS USES, and only one filesystem can live
   there. Flashing a FatFS sketch over a LittleFS one reformats it, and the
   reverse is equally true -- the sketch on the board decides which filesystem
   the board has. If you want the flash readable from a PC as a USB stick, FAT
   is the one to pick; if you only ever read it from sketches, LittleFS wears
   better and has no minimum size.

   Released to the public domain.
*/

#include <FatFS.h>

void listFiles() {
    Dir dir = FatFS.openDir("/");
    int n = 0;
    while (dir.next()) {
        Serial.printf("  %-24s %8u bytes\n", dir.fileName().c_str(),
                      (unsigned)dir.fileSize());
        n++;
    }
    if (!n) {
        Serial.println("  (empty)");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    Serial.println("\nMounting the flash filesystem...");
    if (!FatFS.begin()) {
        /* begin() has four different failure causes and they need different
           answers, so it says which. The message itself is only compiled in
           with -DFS_DEBUG; this string is always available. */
        Serial.print("failed: ");
        Serial.println(ch32h4_fatfs_last_error_string());
        return;
    }

    FSInfo info;
    if (FatFS.info(info)) {
        Serial.printf("%u KB total, %u KB used, %u byte clusters\n",
                      (unsigned)(info.totalBytes / 1024),
                      (unsigned)(info.usedBytes / 1024),
                      (unsigned)info.blockSize);
    }

    if (!FatFS.exists("/hello.txt")) {
        Serial.println("Creating /hello.txt");
        File f = FatFS.open("/hello.txt", "w");
        if (f) {
            f.println("Written by the ListFiles example.");
            f.printf("Boot time was %lu ms.\n", millis());
            /* close() is what commits it. A File left open when the sketch
               resets loses whatever was still buffered. */
            f.close();
        }
    } else {
        Serial.println("/hello.txt is already here -- it survived the reset.");
        File f = FatFS.open("/hello.txt", "r");
        if (f) {
            Serial.println("Its contents:");
            while (f.available()) {
                Serial.write(f.read());
            }
            f.close();
        }
    }

    Serial.println("Directory:");
    listFiles();
}

void loop() {
    delay(1000);
}
