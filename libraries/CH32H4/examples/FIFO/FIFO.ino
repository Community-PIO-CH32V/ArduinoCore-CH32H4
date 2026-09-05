/*
   The inter-core FIFO: eight words each way, in hardware.

   The right way to hand work between the cores. No lock, no shared buffer, no
   memory barrier to get wrong -- push on one side, pop on the other.

   This example code is in the public domain.

   ---
   For the CH32H41x core.

   push() and pop() BLOCK: push spins while the ring is full, pop spins while
   it is empty. That is usually what a producer/consumer pair wants, and it is
   also how a sketch hangs when the other core stops draining. push_nb() and
   pop_nb() are the non-blocking pair, and available() says how many words are
   waiting for THIS core.

   EIGHT WORDS. It is a hand-off, not a buffer -- send a pointer or an index,
   not a message.
*/

#include <CH32H4.h>

void setup() {
  Serial1.begin(115200);
  delay(200);
  Serial1.println("V5F: sending work to the V3F");
}

void loop() {
  static uint32_t n = 0;

  /* Hand the V3F a number to chew on. */
  CH32H4.fifo.push(n);

  /* Wait for the answer. */
  uint32_t result = CH32H4.fifo.pop();

  Serial1.print("V5F: sent ");
  Serial1.print((unsigned)n);
  Serial1.print(", got back ");
  Serial1.println((unsigned)result);

  n++;
  delay(500);
}

void setup1() {
}

void loop1() {
  /* Non-blocking, so this core stays free to do something else. */
  uint32_t v;
  if (CH32H4.fifo.pop_nb(&v)) {
    /* Whatever the "work" is. */
    uint32_t answer = v * v;
    CH32H4.fifo.push(answer);
  }
}
