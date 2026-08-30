/* CDC + HID composite.
 *
 * Adafruit_USBD_HID is constructed as a global, so it registers itself with
 * Adafruit_USBD_Device before TinyUSB_Device_Init() runs in the core's
 * startup. That ordering is the whole mechanism: any number of libraries can
 * add interfaces this way without the core knowing about them, which is how
 * Keyboard, Mouse and Joystick will work.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

static uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(2)),
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report),
                          HID_ITF_PROTOCOL_NONE, 10, false);

void setup() {
  Serial1.begin(115200);
  usb_hid.begin();
  Serial1.println("hidtest ready");
  Serial1.print("hid_ready=");
  Serial1.println(usb_hid.ready() ? 1 : 0);
  Serial1.print("mounted=");
  Serial1.println(TinyUSBDevice.mounted() ? 1 : 0);
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "hidstat") {
          Serial1.print("hid_ready=");
          Serial1.println(usb_hid.ready() ? 1 : 0);
          Serial1.print("mounted=");
          Serial1.println(TinyUSBDevice.mounted() ? 1 : 0);
        }
        Serial1.print("> ");
        line = "";
      }
    } else {
      line += c;
    }
  }
  yield();
}
