/*
   Ticker with a parameter, and Ticker::once().

   attach_ms() also takes a callback that receives a void* -- which is how one
   function serves several tickers, and how a callback reaches an object
   without a global.

   once_ms() fires a single time and detaches itself: a timeout, rather than a
   heartbeat.

   This example code is in the public domain.

   ---
   For the CH32H41x core. Both callbacks run in interrupt context: the shared
   state below is volatile for that reason, and nothing prints from inside
   them.
*/

#include <Ticker.h>

struct Blinker {
  const char *name;
  volatile uint32_t count;
};

static Blinker fast = { "fast", 0 };
static Blinker slow = { "slow", 0 };

Ticker tickFast;
Ticker tickSlow;
Ticker timeout;

static volatile bool expired = false;

void bump(void *arg) {
  ((Blinker *)arg)->count++;
}

void onTimeout() {
  expired = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  tickFast.attach_ms(100, bump, &fast);
  tickSlow.attach_ms(500, bump, &slow);

  /* Fires once, five seconds from now, then detaches itself. */
  timeout.once_ms(5000, onTimeout);

  Serial.println("running -- the timeout stops the fast ticker after 5 s");
}

void loop() {
  if (expired && tickFast.active()) {
    expired = false;
    tickFast.detach();
    Serial.println("timeout: fast ticker detached");
  }

  Serial.print(fast.name);
  Serial.print("=");
  Serial.print((int)fast.count);
  Serial.print("  ");
  Serial.print(slow.name);
  Serial.print("=");
  Serial.println((int)slow.count);
  delay(1000);
}
