/*
   Udp NTP Client

   Get the time from a Network Time Protocol (NTP) time server.

   created 4 Sep 2010
   by Michael Margolis
   modified 9 Apr 2012
   by Tom Igoe

   This example code is in the public domain.

   ---
   For the CH32H41x core, which has an NTP client of its own -- so this shows
   both: the raw UDP exchange the original example performs, and NTP.begin(),
   which does the same thing and then sets the RTC.

   Setting the RTC matters beyond printing the time: TLS certificate validity
   is checked against it, and a board that thinks it is the year 2000 rejects
   every certificate ever issued with an error that says nothing about clocks.
*/

#include <LwipEthernet.h>
#include <NTP.h>

extern "C" {
#include "ch32h4_rtc.h"
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }
  Serial.print("IP address: ");
  Serial.println(Ethernet.localIP());

  /* The RTC does not start itself -- most sketches have no use for a clock,
     and the backup domain costs a few microamps. LSE first; LSI if the board
     has no 32 kHz crystal. */
  if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
  }

  Serial.println("asking pool.ntp.org for the time...");
  NTP.begin("pool.ntp.org");

  if (!NTP.waitSynced(20000)) {
    Serial.println("no answer -- is there a route to the internet?");
    return;
  }

  time_t now = time(nullptr);
  Serial.print("unix time: ");
  Serial.println((long)now);
  Serial.print("UTC:       ");
  Serial.print(ctime(&now));
  Serial.print("rtc set:   ");
  Serial.println(ch32h4_rtc_is_set() ? "yes" : "no");
}

void loop() {
  delay(10000);
  time_t now = time(nullptr);
  Serial.print("still ");
  Serial.print(ctime(&now));
}
