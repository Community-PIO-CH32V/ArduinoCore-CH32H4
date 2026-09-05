/* The CH32H41x itself: both cores, the FIFO, the hardware semaphores, the
 * die temperature, the unique ID.
 *
 * THIS HEADER ADDS NOTHING. Everything it names is in the core already and
 * reachable from any sketch through Arduino.h -- the `CH32H4` object,
 * CH32H4Mutex, analogReadTemp(). The library exists so that the examples have
 * somewhere to live that the IDE will list, which is the same reason
 * arduino-pico ships an `rp2040` library.
 *
 * Including it is harmless and makes the dependency explicit:
 *
 *     #include <CH32H4.h>
 *
 * What is actually here, all of it documented at its own definition:
 *
 *   CH32H4.getCoreNum()      0 on the V3F, 1 on the V5F
 *   CH32H4.getCpuFreqHz()    400 MHz on the V5F, 100 MHz on the V3F
 *   CH32H4.getBusFreqHz()    100 MHz for both -- what a peripheral divides
 *   CH32H4.getFreeHeap()     DTCM first, then the shared half
 *   CH32H4.getUniqueId(buf)  eight bytes, from the factory
 *   CH32H4.fifo              an eight-deep word queue between the cores
 *   CH32H4Mutex              a hardware semaphore, recursive, with a guard
 *   analogReadTemp()         degrees Celsius, from the on-die sensor
 *
 * And the two functions a sketch defines to use the second core at all:
 *
 *   void setup1();           runs on the V3F
 *   void loop1();
 *
 * Note which is which. setup()/loop() run on the V5F -- 400 MHz, out of order,
 * with an instruction cache -- and setup1()/loop1() on the V3F, which is the
 * core that boots. That is the opposite of arduino-pico, where setup() is the
 * boot core; here the boot core is the slow one, and a sketch that says
 * nothing about cores should get the fast one.
 */
#pragma once

#include <Arduino.h>
