/* ArduinoOTA, on Serial1, with the commit under manual control.
 *
 * Not the example: an example blocks on `while (!Serial)`, and the point here
 * is to be able to watch every step from a serial port that is already open.
 *
 * WHY THE COMMIT IS OPT-IN. Committing erases the running sketch. If the image
 * is wrong, or the committer is wrong, the board needs a probe and a
 * `wlink erase` to come back -- so this starts in a mode that receives and
 * verifies the whole image and then deliberately does NOT write it. That
 * exercises the invite, the transfer, the MD5, the staging and the header
 * check without ever opening the window where the board can be lost.
 *
 *     safe    receive and verify only, then report (default)
 *     arm     the next upload really is written to flash
 *     info    address, name, staged size, heap
 *
 * OTA_VERSION is bumped between builds so that "did the new image actually
 * boot" has an answer that is not a guess.
 */
#include <Arduino.h>
#include <LwipEthernet.h>
#include <MDNS.h>
#include <ArduinoOTA.h>

extern "C" {
#include "ch32h417_iwdg.h"
}

/* Arm the independent watchdog before the commit.
 *
 * If the commit hangs, the flash controller is left mid-operation, and in that
 * state the debug probe cannot reach it either -- wlink reports the board as
 * having no MCU attached, and the only way back is holding NRST through a
 * `wlink erase` at the bench. That has already cost one rescue.
 *
 * The IWDG runs off its own oscillator and does not care that interrupts are
 * masked or that flash is busy, so a hang becomes a reset instead. The board
 * still will not boot -- the sketch region is half written either way -- but it
 * is reachable, which is the difference between reflashing it and rescuing it.
 *
 * 40 kHz / 256 = 156 Hz, reload 4095 -> about 26 s. Far longer than a commit,
 * which is a few hundred milliseconds. */
static void armWatchdog() {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_256);
  IWDG_SetReload(4095);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

#ifndef OTA_VERSION
#define OTA_VERSION 1
#endif

static bool armed = false;
static uint32_t lastProgress = 0;

static void report() {
  Serial1.print("version="); Serial1.println(OTA_VERSION);
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());
  Serial1.print("host="); Serial1.println(ArduinoOTA.getHostname());
  Serial1.print("armed="); Serial1.println(armed ? 1 : 0);
  Serial1.print("max_image="); Serial1.println((unsigned)ArduinoOTA.maxImageSize());
  Serial1.print("free_heap="); Serial1.println((unsigned)CH32H4.getFreeHeap());
  Serial1.print("staged="); Serial1.println((unsigned)Update.stagedSize());
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.print("otatest booting, version="); Serial1.println(OTA_VERSION);

  const int rc = Ethernet.begin();
  Serial1.print("eth_begin="); Serial1.println(rc);
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());

  ArduinoOTA.setHostname("ch32h4-ota");

  /* The whole point of the safe mode: everything up to the erase runs, and
     the erase does not. `arm` turns it back on. */
  ArduinoOTA.setRebootOnSuccess(false);

  ArduinoOTA.onStart([]() {
    Serial1.print("ota_start cmd=");
    Serial1.println(ArduinoOTA.getCommand());
    lastProgress = 0;
  });

  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    const uint32_t pct = (uint32_t)((done * 100ull) / total);
    if (pct >= lastProgress + 10) {
      lastProgress = pct;
      Serial1.print("ota_progress="); Serial1.println(pct);
    }
  });

  ArduinoOTA.onEnd([]() {
    Serial1.println("ota_end received_and_verified");
  });

  ArduinoOTA.onError([](ota_error_t e) {
    Serial1.print("ota_error="); Serial1.println((int)e);
    Serial1.print("updater_error="); Serial1.println((int)Update.getError());
    Update.printError(Serial1);
  });

  ArduinoOTA.begin();
  Serial1.println("otatest ready");
  report();
  Serial1.print("> ");
}

void loop() {
  ArduinoOTA.handle();

  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "info") {
          report();
        } else if (line == "safe") {
          armed = false;
          ArduinoOTA.setRebootOnSuccess(false);
          Serial1.println("safe: uploads are received and verified, not written");
        } else if (line == "arm") {
          armed = true;
          ArduinoOTA.setRebootOnSuccess(true);
          Serial1.println("ARMED: the next upload will be written to flash");
        } else if (line == "commit") {
          /* Commit whatever a `safe` upload left staged. Separated from the
             upload so that the two can fail independently. */
          Serial1.print("staged="); Serial1.println((unsigned)Update.stagedSize());
          if (!Update.stagedSize()) {
            Serial1.println("nothing staged");
          } else {
            Serial1.println("committing, see you on the other side");
            Serial1.flush();
            armWatchdog();
            if (!Update.commit()) {
              Serial1.println("commit refused (could not park the other core?)");
            }
          }
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
