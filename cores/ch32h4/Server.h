/* Shim.
 *
 * Libraries include <Server.h> directly, but cores/ch32h4/api is deliberately NOT
 * on the include path: on a case-insensitive filesystem it makes <string.h>
 * resolve to ArduinoCore-API's String.h and every use of strlen, memcpy and
 * memset inside the API fails to compile. So the headers libraries actually
 * reach for get a one-line forwarder here instead.
 *
 * Note there is deliberately no String.h shim, for exactly that reason. Reach
 * String through Arduino.h or api/ArduinoAPI.h.
 */
#pragma once
#include "api/Server.h"
