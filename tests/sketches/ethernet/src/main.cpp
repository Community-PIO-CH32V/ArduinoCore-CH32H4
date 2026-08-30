#include <Arduino.h>
#include "ch32h4_eth.h"

void setup() {
  Serial1.begin(115200);
  Serial1.print("eth_init=");
  Serial1.println(eth_init(&eth_instance));
  Serial1.println("ethtest ready");
  Serial1.print("> ");
}

void loop() { yield(); }
