/*
   BasicOTA - upload a sketch over the network instead of over the probe.

   Flash this once with a probe. After that the board appears in the Arduino
   IDE under Tools -> Port as a network port, and in PlatformIO with
   `upload_protocol = espota`.

   WHAT TO EXPECT THE FIRST TIME:

   The board has to be on the network before it can be uploaded to, which means
   this sketch has to keep running. An upload that hangs the sketch -- a
   while(1), a fault, an empty loop() with no ArduinoOTA.handle() -- takes the
   network port with it, and the next upload has to go over the probe again.
   Keep handle() in loop() in whatever you flash next.

   THE IMAGE IS STAGED IN RAM. There is no second flash slot on this part, so
   the whole image is held in the heap until it has been received and its MD5
   checked. That caps an over-the-air sketch a good deal below what a probe can
   flash; this sketch prints the current limit at startup. An upload that is
   too large is refused at the invite, and the IDE shows why.

   AND THE COMMIT IS NOT POWER-FAIL SAFE. Between the first erase and the last
   program the sketch region is neither the old sketch nor the new one. Losing
   power there leaves a board that needs a probe and a `wlink erase` to
   recover. Verifying before erasing keeps that window as short as it can be,
   but it cannot close it.

   Released to the public domain.
*/

#include <LwipEthernet.h>
#include <ArduinoOTA.h>

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    Serial.println("\nStarting Ethernet...");
    if (!Ethernet.begin()) {
        Serial.println("No link, or no DHCP answer. OTA needs a network.");
        return;
    }
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());

    /* Without a password, anyone who can reach this board on the network can
       replace its firmware. Leaving it out is the Arduino default, and it is
       fine on a bench; it is not fine on a network you do not control. */
    // ArduinoOTA.setPassword("changeme");

    ArduinoOTA.onStart([]() {
        Serial.println(ArduinoOTA.getCommand() == U_FLASH
                       ? "Update starting: sketch"
                       : "Update starting: filesystem");
    });

    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (done * 100) / total);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nReceived and verified. Committing...");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error [%u]: ", error);
        switch (error) {
        case OTA_AUTH_ERROR:    Serial.println("auth failed"); break;
        case OTA_BEGIN_ERROR:   Serial.println("begin failed"); break;
        case OTA_CONNECT_ERROR: Serial.println("connect failed"); break;
        case OTA_RECEIVE_ERROR: Serial.println("receive failed"); break;
        case OTA_END_ERROR:     Serial.println("end failed"); break;
        default:                Serial.println("unknown"); break;
        }
    });

    ArduinoOTA.begin();

    Serial.print("OTA ready as ");
    Serial.print(ArduinoOTA.getHostname());
    Serial.println(".local");
    Serial.print("Largest image that fits in RAM right now: ");
    Serial.print(ArduinoOTA.maxImageSize() / 1024);
    Serial.println(" KB");
}

void loop() {
    /* Everything happens here: the invite, the transfer, and the commit. A
       loop() that blocks for seconds at a time will make uploads unreliable,
       and one that blocks forever makes them impossible. */
    ArduinoOTA.handle();
}
