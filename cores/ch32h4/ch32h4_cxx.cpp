/* The C++ runtime hooks that -nostartfiles leaves out.
 *
 * This file exists because of one specific silent failure, and the comment on
 * __cxa_atexit below is the reason it must not be deleted.
 */
#include <stdlib.h>

extern "C" {

/* Registers a destructor for an object with static storage duration.
 *
 * GCC emits a call to this after constructing ANY function-local static with a
 * non-trivial destructor -- `static String line;` in a sketch's loop() is
 * enough. Under -nostartfiles nothing provides it, and the reference resolves
 * to ZERO. The linker reports nothing: not an undefined symbol, not a warning,
 * not even an entry in `nm -u`. The instruction that comes out is
 *
 *     auipc ra, 0x0
 *     jalr  ra, 0(x0)      <- call absolute address 0
 *
 * and address 0 on this part is _start_v3f. So the first time a sketch's
 * loop() runs, the V5F jumps into the V3F's reset vector and re-runs the other
 * core's startup on the wrong core. The board lockup-resets about 25 times a
 * second, with no fault record, because the fault handler is never reached.
 *
 * It presented as a dual-core bug for a long time, because the sketch that hit
 * it was the dual-core one. It has nothing to do with the second core.
 *
 * Doing nothing and returning success is correct here, not a shortcut: these
 * destructors would run at exit(), and a sketch never exits. Recording them
 * would consume RAM to build a list nothing will ever walk.
 */
int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

/* Same reasoning, for the C spelling. */
int atexit(void (*func)(void)) {
    (void)func;
    return 0;
}

/* The "which shared object owns this static" token __cxa_atexit is passed.
 * There is one program here, so its value is irrelevant -- but the compiler
 * takes its address, and without a definition that reference resolves to zero
 * exactly as above. */
void *__dso_handle = &__dso_handle;

}  /* extern "C" */

/* Called when a pure virtual is invoked, which means an object was used during
 * its own base-class construction or after destruction. Spinning would wedge
 * the debug probe (see docs/hazards.md), so this traps the way every other
 * unrecoverable path here does: through the fault handler, which prints and
 * resets. `ebreak` is what the SDK's own assertions use. */
extern "C" void __cxa_pure_virtual(void) {
    __asm volatile("ebreak");
    for (;;) {
    }
}

extern "C" void __cxa_deleted_virtual(void) {
    __asm volatile("ebreak");
    for (;;) {
    }
}
