/*
   LittleFS: walk the filesystem, and report what is left.

   Directory iteration plus the space accounting a sketch needs before it
   writes anything -- a log that fills the partition and then fails silently is
   the classic embedded filesystem bug.

   This example code is in the public domain.

   ---
   For the CH32H41x core. Set a filesystem size first (board_build.filesystem_size,
   or Tools > Filesystem size); with none, begin() fails and says so.
*/

#include <LittleFS.h>

void listDir(const char *path, int indent) {
  Dir dir = LittleFS.openDir(path);
  while (dir.next()) {
    for (int i = 0; i < indent; i++) {
      Serial.print("  ");
    }
    Serial.print(dir.fileName());
    if (dir.isDirectory()) {
      Serial.println("/");
      String sub = String(path);
      if (!sub.endsWith("/")) {
        sub += "/";
      }
      sub += dir.fileName();
      listDir(sub.c_str(), indent + 1);
    } else {
      Serial.print("  ");
      Serial.print(dir.fileSize());
      Serial.println(" bytes");
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS.begin() failed -- is the filesystem size set?");
    return;
  }

  /* Make something to look at, if the filesystem is empty. */
  if (!LittleFS.exists("/readme.txt")) {
    LittleFS.mkdir("/logs");
    File f = LittleFS.open("/readme.txt", "w");
    f.println("written by the ListFiles example");
    f.close();
    f = LittleFS.open("/logs/first.log", "w");
    f.println("log line");
    f.close();
  }

  Serial.println("filesystem:");
  listDir("/", 1);

  FSInfo info;
  if (LittleFS.info(info)) {
    Serial.println();
    Serial.print("total  ");
    Serial.println(info.totalBytes);
    Serial.print("used   ");
    Serial.println(info.usedBytes);
    Serial.print("free   ");
    Serial.println(info.totalBytes - info.usedBytes);
  }
}

void loop() {
}
