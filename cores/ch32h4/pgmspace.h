/* <pgmspace.h>, the spelling the esp8266 lineage of libraries uses.
 *
 * On AVR this named a separate address space that needed its own load
 * instructions. This part has one flat address space, so PROGMEM is empty and
 * every pgm_read_* is an ordinary dereference -- which is exactly what the
 * compat header next door defines. This file exists only so that code written
 * as #include <pgmspace.h> finds it.
 */
#pragma once

#include <avr/pgmspace.h>

/* FPSTR and PGM_VOID_P, which the AVR compat header does not carry but the
   esp8266 lineage of libraries uses. FPSTR wraps a flash string pointer so it
   can be handed to String and Print; with one flat address space there is
   nothing to wrap, so it is a cast to the type those overloads expect. */
#ifndef FPSTR
#define FPSTR (const char *)
#endif

#ifndef PGM_VOID_P
#define PGM_VOID_P const void *
#endif
