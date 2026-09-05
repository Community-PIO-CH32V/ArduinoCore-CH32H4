/* Parking the other core for the duration of a flash write.
 *
 * A page program does not complete while the other core is fetching from
 * flash: the board hangs, and only the watchdog gets it back. ch32h4_park.c
 * says why the two cores differ; docs/hazards.md has the measurements.
 *
 * ch32h4_flash_erase() and ch32h4_flash_write() call these themselves, so
 * LittleFS, EEPROM and everything else that writes flash are safe without
 * knowing any of it exists. They are public for a caller that wants to park
 * once around a batch rather than once per page, which is what an OTA
 * committer wants.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Park this core if the other one has asked. Called from the V3F main loop
 * and from yield(), which is every delay() and every blocking read. Cheap: a
 * load and a branch when nobody is asking. */
void ch32h4_park_check(void);

/* "This core is now running code that fetches from flash, so park me before
 * writing it." The V3F calls this when it takes up setup1()/loop1(); a V3F
 * with neither sleeps in stop mode, fetches nothing, and never does. */
void ch32h4_park_live_here(void);

/* Ask the other core into its ITCM spin and wait until it says it is there.
 *
 * True when it is safe to write flash -- including when the other core is not
 * running at all, which is the common case. FALSE MEANS DO NOT WRITE: the
 * other core did not park within the timeout, and writing anyway is the hang
 * this exists to prevent. That happens when loop1() runs for a long time
 * without returning or calling yield().
 *
 * Nested calls are not counted. Park once around the whole operation. */
bool ch32h4_park_other(uint32_t timeout_ms);

void ch32h4_unpark_other(void);

#ifdef __cplusplus
}
#endif
