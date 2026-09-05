/*
   UpdateFromHTTP - fetch a new firmware image from a web server and install it.

   ArduinoOTA is the push model: you upload from the IDE and the board listens.
   This is the pull model -- the board decides when to update and fetches the
   image itself, which is what you want for a device in a cupboard that should
   check for new firmware on its own schedule.

   WHAT TO PUT ON THE SERVER: firmware_ota.bin from the build, NOT firmware.bin.
   The full binary starts with the V3F boot stub and carries the sketch 32 KB
   further in; installing that would put the stub where the sketch belongs.
   Updater checks and refuses, but sending the right file is better than being
   told off for sending the wrong one.

   Under PlatformIO the file is in .pio/build/<env>/firmware_ota.bin. From the
   Arduino IDE, use Sketch -> Export Compiled Binary and take the *_ota.bin.

   THE IMAGE IS STAGED IN RAM. There is no second flash slot on this part, so
   the whole image is held in the heap until it has been received and its MD5
   verified. That caps an over-the-air image well below what a probe can flash;
   Updater::maxImageSize() reports what fits right now.

   AND COMMITTING IS NOT POWER-FAIL SAFE. Between the first erase and the last
   program the sketch region is neither the old image nor the new one. Losing
   power there leaves a board that needs a probe to recover. Verifying before
   erasing keeps that window as short as it can be; it cannot close it.

   Released to the public domain.
*/

#include <LwipEthernet.h>
#include <HTTPClient.h>
#include <Updater.h>

/* Where the image lives, and what it should hash to.
 *
 * THE MD5 IS NOT OPTIONAL in any real deployment. Without it a truncated
 * download or a corrupted proxy response is indistinguishable from a good
 * image, and you find out by bricking the board. Publish the md5 of
 * firmware_ota.bin alongside it -- `md5sum firmware_ota.bin` -- and read it
 * from the server too if it changes per release. */
const char *FIRMWARE_URL = "http://192.168.0.10/firmware_ota.bin";
const char *FIRMWARE_MD5 = "0123456789abcdef0123456789abcdef";

static bool updateFirmware() {
    HTTPClient http;
    if (!http.begin(FIRMWARE_URL)) {
        Serial.println("bad URL");
        return false;
    }

    const int status = http.GET();
    if (status != 200) {
        Serial.print("HTTP ");
        Serial.println(status);
        http.end();
        return false;
    }

    const int len = http.getSize();
    if (len <= 0) {
        /* A chunked response has no length, and this needs one: the whole
           image has to be allocated up front. Serve it with Content-Length. */
        Serial.println("server did not give a Content-Length");
        http.end();
        return false;
    }

    Serial.print("image is ");
    Serial.print(len);
    Serial.print(" bytes, largest that fits is ");
    Serial.println((unsigned)UpdaterClass::maxImageSize());

    if (!Update.begin(len)) {
        Update.printError(Serial);
        http.end();
        return false;
    }
    Update.setMD5(FIRMWARE_MD5);

    Update.onProgress([](size_t done, size_t total) {
        Serial.printf("  %u%%\r", (unsigned)((done * 100ull) / total));
    });

    /* Straight from the socket into the staging buffer. Nothing has touched
       flash at this point and nothing will until commit(). */
    const size_t written = Update.writeStream(*http.getStreamPtr());
    http.end();

    if (written != (size_t)len) {
        Serial.print("\nshort read: ");
        Serial.println((unsigned)written);
        Update.printError(Serial);
        return false;
    }

    /* end() checks the MD5 and that the image really is a sketch for this
       board. Everything is still recoverable if it says no. */
    if (!Update.end()) {
        Serial.println();
        Update.printError(Serial);
        return false;
    }

    Serial.println("\nreceived and verified, committing");
    Serial.flush();

    /* DOES NOT RETURN when it succeeds: it erases the sketch region, writes
       the staged image over it from ITCM, and resets the part. It returns only
       if it could not get the other core parked, in which case nothing has
       been touched. */
    if (!Update.commit()) {
        Serial.println("commit refused; firmware unchanged");
        return false;
    }
    return true;   // not reached
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    if (!Ethernet.begin()) {
        Serial.println("no network");
        return;
    }
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());

    Serial.println("Send 'u' to update.");
}

void loop() {
    if (Serial.available() && Serial.read() == 'u') {
        if (!updateFirmware()) {
            Serial.println("update failed, still running the old firmware");
        }
    }
    delay(10);
}
