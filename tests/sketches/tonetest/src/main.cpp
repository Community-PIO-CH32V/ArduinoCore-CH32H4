#include <Arduino.h>
#include <Ticker.h>

static volatile uint32_t tickCount = 0;
static void onTick() { tickCount++; }
Ticker ticker;

void setup() {
  Serial1.begin(115200);
  Serial1.println("tonetest ready");
  Serial1.print("> ");
}

void handle(const String &cmd) {
  if (cmd == "tonetest") {
    tone(PA0, 1000);
    ch32h4_pwm_af_t af;
    bool found = false;
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
      if (g_pwm_af_map[i].pin == PA0 &&
          ch32h4_timer_owner(g_pwm_af_map[i].timer) == CH32H4_TIMER_TONE) {
        af = g_pwm_af_map[i]; found = true; break;
      }
    }
    Serial1.print("tone_claimed="); Serial1.println(found ? 1 : 0);
    if (found) {
      Serial1.print("tone_timer="); Serial1.println(af.timer);
      Serial1.print("tone_owner=");
      Serial1.println(ch32h4_timer_owner_name(ch32h4_timer_owner(af.timer)));
    }
    noTone(PA0);
    Serial1.print("tone_released=");
    Serial1.println(found && ch32h4_timer_owner(af.timer) == CH32H4_TIMER_FREE ? 1 : 0);

  } else if (cmd == "tickertest") {
    tickCount = 0;
    ticker.attach_ms(10, onTick);
    uint32_t t0 = millis();
    while (millis() - t0 < 200) { yield(); }
    ticker.detach();
    Serial1.print("ticker_fired="); Serial1.println(tickCount);
    /* 200 ms at 10 ms should be ~20; allow for scheduling slop. */
    Serial1.print("ticker_in_range=");
    Serial1.println((tickCount >= 15 && tickCount <= 25) ? 1 : 0);

  } else if (cmd == "tickeronce") {
    tickCount = 0;
    ticker.once_ms(20, onTick);
    uint32_t t0 = millis();
    while (millis() - t0 < 150) { yield(); }
    Serial1.print("once_fired="); Serial1.println(tickCount);
    Serial1.print("once_is_one=");
    Serial1.println(tickCount == 1 ? 1 : 0);
    Serial1.print("once_inactive=");
    Serial1.println(ticker.active() ? 0 : 1);
  }
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') { if (line.length()) { handle(line); line = ""; } }
    else { line += c; }
  }
  yield();
}
