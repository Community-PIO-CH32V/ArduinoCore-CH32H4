/* The pin table for this board, which is the package's.
 *
 * Included rather than duplicated: port, bit and ADC channel are decided by
 * the die and its bonding, so every board on a CH32H417QEU6 has the same
 * table. It used to be written out here, and was byte-identical to the
 * generated one -- all 96 entries, ADC channels included -- which is what
 * made moving it safe.
 *
 * Both build systems compile only the ONE variant directory a board names, so
 * a file sitting in the package directory would never be built. Hence the
 * include, the same arrangement pin_map.c uses next door.
 */
#include "../CH32H417QEU6/pins_table_package.c"
