/* Both FatFs volumes mounted at once: flash and SD card.
 *
 * THIS IS THE CASE THE RESTRUCTURE EXISTS FOR. Every other test here mounts
 * one volume, and a single-volume FatFs quietly serving whichever mounted
 * first would pass all of them. Only this fails.
 *
 * On Serial1, no waiting for a host. The SD card must be wired to the SDMMC
 * default mapping -- CK on PC12, CMD on PD2, D0 on PC8 -- and the tests skip
 * rather than fail without one: an unwired bench is a missing precondition,
 * not a broken driver.
 */
#include <Arduino.h>
#include <string.h>

#include <FatFS.h>
#include <SDFS.h>

static bool flashUp = false;
static bool sdUp = false;

/* Content derived from the volume's name, so reading the wrong volume's file
   is a failure rather than a coincidence -- which is the entire point. */
static String payload(const char *vol, int i) {
  return String(vol) + " line " + String(i);
}

static bool writeFile(FS &fs, const char *vol) {
  String path = String("/") + vol + ".txt";
  File f = fs.open(path.c_str(), "w");
  if (!f) {
    return false;
  }
  for (int i = 0; i < 32; i++) {
    f.println(payload(vol, i));
  }
  f.close();
  return true;
}

static bool readFile(FS &fs, const char *vol) {
  String path = String("/") + vol + ".txt";
  File f = fs.open(path.c_str(), "r");
  if (!f) {
    return false;
  }
  bool ok = true;
  for (int i = 0; i < 32 && ok; i++) {
    String got = f.readStringUntil('\n');
    got.trim();
    ok = (got == payload(vol, i));
  }
  f.close();
  return ok;
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("twovoltest starting");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        int sp = line.indexOf(' ');
        String cmd = sp < 0 ? line : line.substring(0, sp);
        String arg = sp < 0 ? String() : line.substring(sp + 1);

        if (cmd == "both") {
          /* Mount both, in this order, and report each independently. The
             flash volume needs no card; the SD one may legitimately be
             absent. */
          flashUp = FatFS.begin();
          Serial1.print("flash_mount="); Serial1.println(flashUp ? 1 : 0);
          Serial1.print("flash_err=");
          Serial1.println(ch32h4_fatfs_last_error_string());

          sdUp = SDFS.begin();
          Serial1.print("sd_present="); Serial1.println(sdUp ? 1 : 0);
          Serial1.print("sd_mount="); Serial1.println(sdUp ? 1 : 0);

          /* Both at once is the claim; prove the sizes come from different
             devices rather than one volume answering twice. */
          FSInfo fi;
          if (flashUp && FatFS.info(fi)) {
            Serial1.print("flash_kb=");
            Serial1.println((uint32_t)(fi.totalBytes / 1024));
          }
          if (sdUp && SDFS.info(fi)) {
            Serial1.print("sd_kb=");
            Serial1.println((uint32_t)(fi.totalBytes / 1024));
          }
        } else if (cmd == "format") {
          Serial1.print("flash_format=");
          Serial1.println(FatFS.format() ? 1 : 0);
        } else if (cmd == "write") {
          bool f = flashUp && writeFile(FatFS, "flash");
          bool s = sdUp && writeFile(SDFS, "sd");
          Serial1.print("flash_write="); Serial1.println(f ? 1 : 0);
          Serial1.print("sd_write="); Serial1.println(s ? 1 : 0);
        } else if (cmd == "read") {
          bool f = flashUp && readFile(FatFS, "flash");
          bool s = sdUp && readFile(SDFS, "sd");
          Serial1.print("flash_match="); Serial1.println(f ? 1 : 0);
          Serial1.print("sd_match="); Serial1.println(s ? 1 : 0);
        } else if (cmd == "interleave") {
          /* Alternate between the volumes rather than finishing one first.
             A shared FatFs work area, a shared drive number, a static that
             should have been per-volume -- none of those survive this, and
             all of them survive writing one volume then the other. */
          int n = arg.toInt();
          if (n <= 0 || n > 64) { n = 16; }
          bool ok = flashUp && sdUp;
          File ff, sf;
          if (ok) {
            ff = FatFS.open("/inter.txt", "w");
            sf = SDFS.open("/inter.txt", "w");
            ok = ff && sf;
          }
          for (int i = 0; i < n && ok; i++) {
            ok = ff.println(payload("flash", i)) > 0;
            if (ok) { ok = sf.println(payload("sd", i)) > 0; }
          }
          if (ff) { ff.close(); }
          if (sf) { sf.close(); }
          Serial1.print("interleave_write="); Serial1.println(ok ? 1 : 0);

          /* Read both back, again alternating. */
          bool fok = false, sok = false;
          if (ok) {
            File fr = FatFS.open("/inter.txt", "r");
            File sr = SDFS.open("/inter.txt", "r");
            fok = fr; sok = sr;
            for (int i = 0; i < n && fok && sok; i++) {
              String a = fr.readStringUntil('\n'); a.trim();
              String b = sr.readStringUntil('\n'); b.trim();
              fok = (a == payload("flash", i));
              sok = (b == payload("sd", i));
            }
            if (fr) { fr.close(); }
            if (sr) { sr.close(); }
          }
          Serial1.print("flash_match="); Serial1.println(fok ? 1 : 0);
          Serial1.print("sd_match="); Serial1.println(sok ? 1 : 0);
        } else if (cmd == "crosscheck") {
          /* Neither volume may contain the other's file. If one FatFs volume
             were serving both mounts, these would find each other. */
          Serial1.print("flash_has_sd_file=");
          Serial1.println(flashUp && FatFS.exists("/sd.txt") ? 1 : 0);
          Serial1.print("sd_has_flash_file=");
          Serial1.println(sdUp && SDFS.exists("/flash.txt") ? 1 : 0);
        } else {
          Serial1.print("unknown: "); Serial1.println(cmd);
        }
        line = "";
      }
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
