/* The package's pin tables, compiled as part of this board.
 *
 * Included rather than referenced because of how the two build systems find
 * variant sources: both compile the ONE directory a board names and nothing
 * else, so a table living beside them in the package directory would never be
 * built. One line here costs nothing and keeps the tables in one place.
 */
#include "../CH32H417xx_QEU6/pin_map_package.c"
