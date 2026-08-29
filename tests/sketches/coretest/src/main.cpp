/* The on-target half of the test suite.
 *
 * Every hardware test drives this sketch over the console: it reads a command
 * line and prints one or more `key=value` lines, then a "> " prompt. Keeping
 * the assertions on the host side means a test can compare the board against
 * the HOST's clock rather than against a number the board reported about
 * itself.
 *
 * In M1 the V3F never wakes the V5F until task 5, so setup() and loop() are
 * not reached yet.
 */
void setup() {}
void loop() {}
