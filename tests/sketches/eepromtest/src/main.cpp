#include <Arduino.h>
#include <EEPROM.h>

void setup() {
  Serial1.begin(115200);
  Serial1.print("eeprom_begin=");
  Serial1.println(EEPROM.begin(1024) ? 1 : 0);
  Serial1.print("eeprom_len=");
  Serial1.println((uint32_t)EEPROM.length());
  Serial1.println("eepromtest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "eewrite") {
          for (int i = 0; i < 16; i++) { EEPROM.write(i, (uint8_t)(i * 7 + 3)); }
          Serial1.print("ee_commit=");
          Serial1.println(EEPROM.commit() ? 1 : 0);
          Serial1.print("ee_page=");
          Serial1.println(EEPROM.activePage());
        } else if (line == "eeread") {
          bool ok = true;
          for (int i = 0; i < 16; i++) {
            if (EEPROM.read(i) != (uint8_t)(i * 7 + 3)) { ok = false; break; }
          }
          Serial1.print("ee_readback=");
          Serial1.println(ok ? "ok" : "FAIL");
          Serial1.print("ee_page=");
          Serial1.println(EEPROM.activePage());
        } else if (line == "eealternate") {
          /* Two commits must land on different pages: that alternation is
             what guarantees a valid copy exists at every instant. */
          EEPROM.write(0, 0x11); EEPROM.commit();
          int p1 = EEPROM.activePage();
          EEPROM.write(0, 0x22); EEPROM.commit();
          int p2 = EEPROM.activePage();
          Serial1.print("ee_page_a="); Serial1.println(p1);
          Serial1.print("ee_page_b="); Serial1.println(p2);
          Serial1.print("ee_alternated=");
          Serial1.println((p1 != p2) ? 1 : 0);
          Serial1.print("ee_value=0x");
          Serial1.println(EEPROM.read(0), HEX);
        } else if (line == "eeerased") {
          /* Erased flash on this part reads 0xE339E339, not 0xFFFFFFFF. */
          Serial1.print("erased_word=0x");
          Serial1.println(CH32H4_FLASH_ERASED_WORD, HEX);
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
