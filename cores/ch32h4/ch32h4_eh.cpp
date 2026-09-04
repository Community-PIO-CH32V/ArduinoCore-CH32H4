/* Exception support.
 *
 * One libstdc++.a serves both settings -- unlike arduino-pico, which ships a
 * second prebuilt library -- because -fno-exceptions only changes codegen for
 * our own sources.
 *
 * The file is in two halves, and which half is conditional matters. Frame
 * registration is only needed when something can throw, so it is behind
 * CH32H4_EXCEPTIONS. The terminate handler is NOT: std::terminate is reachable
 * with -fno-exceptions too -- a pure virtual call, a failed operator new, a
 * failed static-init guard -- so leaving it out means the default handler, and
 * the 43 KB name demangler behind it, is linked into a build that cannot throw
 * at all.
 *
 * That is not hypothetical. It is what this core shipped: the whole file was
 * behind CH32H4_EXCEPTIONS, so the exceptions-OFF build -- the default, chosen
 * to save space -- came out 35 KB LARGER than the exceptions-on one.
 */
#include "ch32h4_console.h"

#ifdef CH32H4_EXCEPTIONS

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

#endif /* CH32H4_EXCEPTIONS */

/* libstdc++'s documented customisation point, defined UNCONDITIONALLY.
 *
 * Defining it ourselves stops the linker pulling in vterminate.o, and with it
 * the C++ name demangler -- 43 KB of .text whose only caller is the default
 * handler's attempt to print the type name of the exception that got away.
 * Nothing else reaches it, and a board with no debugger attached cannot do
 * anything with the name anyway.
 *
 * The message no longer names exceptions: in a -fno-exceptions build, arriving
 * here means a pure virtual call, a failed allocation or a recursive static
 * initialisation rather than a throw.
 */
namespace __gnu_cxx {
void __verbose_terminate_handler() {
    ch32h4_console_puts("\nterminate called\n");
    ch32h4_console_flush();
    for (;;) {
    }
}
}  // namespace __gnu_cxx
