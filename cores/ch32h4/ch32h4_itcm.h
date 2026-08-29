/* Placing code in ITCM.
 *
 * ITCM is zero-wait at the V5F's core clock; flash is roughly a 25 MHz
 * equivalent behind a 32 KB I-cache. That makes placement a real lever, and
 * also a maintenance burden, so this core runs everything from flash by
 * default and moves only what has been MEASURED as hot.
 *
 * ITCM is 128 KB and most of it is meant to stay free -- the heap is the
 * priority, and there is no point paying for placement that has not been shown
 * to be worth it. arduino-pico's __not_in_flash_func is the same idea.
 *
 * Usage:
 *     __itcm_func void fast_thing(void) { ... }
 */
#pragma once

#define __itcm_func  __attribute__((section(".itcm_text"), noinline))
