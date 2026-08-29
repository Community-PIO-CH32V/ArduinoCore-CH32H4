#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

/* Two disjoint heap regions.
 *
 * DTCM's remainder comes first -- zero-wait at the V5F's 400 MHz -- and when
 * it runs out sbrk continues into the shared region, which runs at HCLK and is
 * larger. newlib's dlmalloc starts a NEW SEGMENT when sbrk returns memory that
 * is not contiguous with the previous top, rather than assuming contiguity, so
 * no single allocation ever spans the gap.
 *
 * About 700 KB in total on an empty sketch. printf allocates its stdout buffer
 * from the heap on first use, which on a part where the heap is whatever is
 * left over would be a first-use failure mode; here it is a non-issue, and
 * that is deliberate.
 */
extern char _heap_dtcm_start[], _heap_dtcm_end[];
extern char _heap_shared_start[], _heap_shared_end[];

static char *s_brk = NULL;
static int   s_region = 0;      /* 0 = DTCM, 1 = shared */

void *_sbrk(ptrdiff_t incr) {
    if (s_brk == NULL) {
        s_brk = _heap_dtcm_start;
    }

    char *limit = (s_region == 0) ? _heap_dtcm_end : _heap_shared_end;

    if (s_brk + incr > limit) {
        if (s_region == 0) {
            /* DTCM is full. Move to the shared region and start again at its
             * base; the discontinuity is deliberate and dlmalloc handles it by
             * opening a second segment. Whatever is left at the end of DTCM is
             * given up, which is at most one allocation's worth. */
            s_region = 1;
            s_brk = _heap_shared_start;
            if (s_brk + incr <= _heap_shared_end) {
                char *prev = s_brk;
                s_brk += incr;
                return prev;
            }
        }
        errno = ENOMEM;
        return (void *)-1;
    }

    char *prev = s_brk;
    s_brk += incr;
    return prev;
}

size_t ch32h4_heap_free(void) {
    if (s_brk == NULL) {
        s_brk = _heap_dtcm_start;
    }
    size_t dtcm = (s_region == 0) ? (size_t)(_heap_dtcm_end - s_brk) : 0u;
    size_t shared = (s_region == 0)
        ? (size_t)(_heap_shared_end - _heap_shared_start)
        : (size_t)(_heap_shared_end - s_brk);
    return dtcm + shared;
}
