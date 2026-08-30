#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* The board's display is on I2C1, PB6 SCL / PB7 SDA. Its address is probed
 * rather than assumed: both 0x3C and 0x3D are shipped and the module does not
 * say which it is. */
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
  Serial1.begin(115200);
  Wire.begin();

  bool ok = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!ok) {
    ok = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }
  Serial1.print("ssd1306_init=");
  Serial1.println(ok ? "ok" : "absent");

  if (ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("CH32H417"));
    display.println(F("Arduino core"));
    display.display();
  }
  Serial1.print("> ");
}

void loop() {
  yield();
}
