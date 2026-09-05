/*
   Both cores, both printing.

   Defining setup1() and loop1() is the whole of starting the second core.
   They run on the V3F at 100 MHz while setup() and loop() run on the V5F at
   400 MHz, in the same image, sharing .data and .bss.

   This example code is in the public domain.

   ---
   For the CH32H41x core.

   NOTHING HERE NEEDS A MUTEX, and that is the point of the example.

   Serial1 IS SAFE FROM BOTH CORES. USART1 is shared with the boot console, so
   every write takes a hardware semaphore -- once for a whole buffer, so a
   println() from one core cannot be cut in half by the other. The two cores
   interleave by line, never by character.

   Serial (USB CDC) IS THE V5F's ALONE. The USB device task refuses to run on
   the V3F, because two cores inside TinyUSB's event queue corrupt it
   silently. A V3F that prints to Serial queues bytes nothing will flush. Use
   Serial1 from the V3F, as this does.

   Anything else two cores touch at once is yours to protect: see the Mutex
   example.
*/

#include <CH32H4.h>

static volatile uint32_t shared = 0;

void setup() {
  Serial1.begin(115200);
  delay(200);
  Serial1.println("V5F: setup()");
}

void loop() {
  Serial1.print("V5F (core ");
  Serial1.print(CH32H4.getCoreNum());
  Serial1.print(") shared=");
  Serial1.println((unsigned)shared);
  delay(1000);
}

void setup1() {
  delay(400);
  Serial1.println("V3F: setup1()");
}

void loop1() {
  /* One writer, one reader, one word: safe without a lock on any machine that
     writes a 32-bit word atomically, which this one does. Two writers, or
     anything wider than a word, would not be -- that is what the Mutex
     example is for. */
  shared++;
  Serial1.print("V3F (core ");
  Serial1.print(CH32H4.getCoreNum());
  Serial1.println(") ticked");
  delay(1500);
}
