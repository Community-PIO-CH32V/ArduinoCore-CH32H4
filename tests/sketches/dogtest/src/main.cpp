/* Watchdog and sleep.
 *
 * Nothing is armed at boot, deliberately. The IWDG cannot be switched off once
 * started, so a sketch that armed it in setup() would leave the board resetting
 * on a cadence for the rest of the session -- including while the next sketch
 * was being flashed. Every command here is explicit, and the one that starves
 * the watchdog says so in its name.
 */
#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

static char line[64];
static int len = 0;

static void handle(const char *cmd) {
  if (!strncmp(cmd, "dogsleep ", 9)) {
    const uint32_t want = (uint32_t)atol(cmd + 9);
    const uint32_t t0 = millis();
    const int slept = Watchdog.sleep((int)want);
    const uint32_t wall = millis() - t0;
    Serial1.print("dog_slept="); Serial1.println(slept);
    Serial1.print("dog_wall="); Serial1.println(wall);

  } else if (!strncmp(cmd, "dogarm ", 7)) {
    /* Arms for real. Every later command feeds it, so the board survives as
       long as the host keeps talking to it. */
    const int t = Watchdog.enable(atoi(cmd + 7));
    Serial1.print("dog_programmed="); Serial1.println(t);
    Serial1.print("dog_enabled="); Serial1.println(Watchdog.isEnabled() ? 1 : 0);

  } else if (!strcmp(cmd, "dogfeed")) {
    Watchdog.reset();
    Serial1.println("dog_fed=1");

  } else if (!strncmp(cmd, "dogstarve ", 10)) {
    /* Arm and stop feeding. The board WILL reset; the host sees the boot
       banner again and times it. Nothing is armed after that reset, so the
       board comes back usable. */
    const int t = Watchdog.enable(atoi(cmd + 10));
    Serial1.print("dog_starving="); Serial1.println(t);
    Serial1.flush();
    for (;;) {
      /* No feed, no yield: just wait to be shot. */
    }
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("dogtest ready");
  Serial1.print("> ");
}

void loop() {
  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) { line[len] = 0; handle(line); len = 0; }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  yield();
}
