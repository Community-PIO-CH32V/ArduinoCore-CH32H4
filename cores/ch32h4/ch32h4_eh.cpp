/* Exception support.
 *
 * Only compiled into anything when the build asks for exceptions. One
 * libstdc++.a serves both settings -- unlike arduino-pico, which ships a
 * second prebuilt library -- because -fno-exceptions only changes codegen for
 * our own sources.
 */
#ifdef CH32H4_EXCEPTIONS

#include "ch32h4_console.h"

extern "C" {

/* Under -nostartfiles NOTHING registers .eh_frame.
 *
 * This libgcc uses the registry-based FDE lookup, and crtbegin's frame_dummy
 * -- which would normally do the registering -- never runs. Without this,
 * __cxa_throw has no FDE, phase 1 of unwinding ends immediately, and every
 * throw reaches std::terminate. It links perfectly cleanly; the failure
 * appears only at run time, which is exactly the shape of the --specs=nano
 * trap this core bans for the same reason.
 *
 * Priority 101 so it runs before any user constructor that might throw.
 */
extern char __eh_frame_start[];
void __register_frame_info(const void *, void *);

/* libgcc keeps a pointer to this and never assumes what it points at, so it
 * only has to outlive the program. */
static char s_eh_object[64];

__attribute__((constructor(101)))
static void ch32h4_register_eh_frame(void) {
    __register_frame_info(__eh_frame_start, s_eh_object);
}

}  /* extern "C" */

/* libstdc++'s documented customisation point. Defining it ourselves stops the
 * linker pulling in vterminate.o, and with it the C++ name demangler -- 43 KB
 * of .text reachable from nowhere else. */
namespace __gnu_cxx {
void __verbose_terminate_handler() {
    ch32h4_console_puts("\nterminate called: an exception escaped\n");
    ch32h4_console_flush();
    for (;;) {
    }
}
}  // namespace __gnu_cxx

#endif /* CH32H4_EXCEPTIONS */
