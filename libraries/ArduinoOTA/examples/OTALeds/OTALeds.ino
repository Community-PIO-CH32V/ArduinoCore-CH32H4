/*
   OTALeds - show what an over-the-air update is doing, on the board's LEDs.

   The same OTA as BasicOTA, with the callbacks wired to the board's two LEDs
   instead of only to Serial. Worth having because an update is the one time
   you may NOT have a serial port attached -- the board is somewhere else, on
   the network, which is the whole point of updating it this way.

   LED1 is on while an update is in progress and LED2 blinks in proportion to
   the progress, so a glance says "receiving, about half way" without a
   terminal. Both go out when the board reboots into the new firmware; if LED1
   stays on and nothing happens, the transfer stalled.

   ON THIS BOARD THE LEDS ARE NOT WIRED TO A PIN by default. Both are fitted,
   but each sits behind a header pin that has to be jumpered to the pin below
   it: LED1 to PE2, LED2 to PE3. Without the jumpers this runs perfectly and
   lights nothing.

   Note that PE2 and PE3 are also SPI4's clock and chip select -- the slave-mode
   SPI -- so a board with these jumpers on cannot also run SPI4 as a slave on
   its default pins.

   Adapted from the ESP8266 core's OTALeds example, by way of arduino-pico.
   Released to the public domain.
*/

#include <LwipEthernet.h>
#include <ArduinoOTA.h>

void setup() {
    pinMode(PIN_LED1, OUTPUT);
    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    digitalWrite(PIN_LED2, LOW);

    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    if (!Ethernet.begin()) {
        Serial.println("No link, or no DHCP answer. OTA needs a network.");
        /* Both LEDs on: there is nothing to wait for, and a board sitting dark
           is indistinguishable from one that is not powered. */
        digitalWrite(PIN_LED1, HIGH);
        digitalWrite(PIN_LED2, HIGH);
        return;
    }
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());

    ArduinoOTA.onStart([]() {
        digitalWrite(PIN_LED1, HIGH);
    });

    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        /* Blink faster as it goes: the LED toggles every 2% at the start and
           every whole percent near the end. Any visible change at all is the
           useful signal -- a frozen LED means a stalled transfer. */
        const unsigned int pct = (done * 100u) / total;
        digitalWrite(PIN_LED2, (pct % 2) ? HIGH : LOW);
    });

    ArduinoOTA.onEnd([]() {
        /* Received and verified. The commit and reboot happen next, and the
           LEDs go out with them. */
        digitalWrite(PIN_LED2, LOW);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        (void)error;
        /* Three quick flashes of both, then back to idle: the update failed
           and the old firmware is still running, which is worth distinguishing
           from a board that rebooted. */
        for (int i = 0; i < 3; i++) {
            digitalWrite(PIN_LED1, HIGH);
            digitalWrite(PIN_LED2, HIGH);
            delay(100);
            digitalWrite(PIN_LED1, LOW);
            digitalWrite(PIN_LED2, LOW);
            delay(100);
        }
    });

    ArduinoOTA.begin();

    Serial.print("OTA ready as ");
    Serial.print(ArduinoOTA.getHostname());
    Serial.println(".local");
}

void loop() {
    ArduinoOTA.handle();

    /* A slow heartbeat while idle, so that "waiting for an update" and "hung"
       do not look the same. onProgress() takes this LED over during a
       transfer. */
    digitalWrite(PIN_LED2, (millis() % 2000) < 50 ? HIGH : LOW);
}
