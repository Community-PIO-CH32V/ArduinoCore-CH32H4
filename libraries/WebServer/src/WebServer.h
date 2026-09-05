/* PORTED FOR THE CH32H41x ARDUINO CORE.
 *
 * Taken from arduino-pico, which took it from the esp8266 core. The only
 * systematic change is that WiFiClient has become arduino::Client: this code
 * only ever used a client through connected(), available(), read(), write()
 * and stop(), all of which are on the Client interface, so naming that
 * interface instead of one implementation of it is what makes the library
 * work over Ethernet here. WiFiServer likewise becomes the server type the
 * template is instantiated with.
 *
 * Licence unchanged: LGPL-2.1-or-later, as below.
 */
/*
    WebServer.h - Create a WebServer class
    Copyright (c) 2022 Earle F. Philhower, III All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
    Modified 8 May 2015 by Hristo Gochkov (proper post and file upload handling)
*/

#pragma once

#include <EthernetServer.h>

#include "WebServerTemplate.h"
#include "detail/mimetable.h"

/* The default server type is Ethernet, because it is the only transport this
   part has. WebServerTemplate takes any server exposing a ClientType, so a
   future transport needs one line here and nothing else.

   HTTPS is WebServerSecure.h, deliberately a separate header: including it is
   what drags in mbedTLS, and a sketch serving plain HTTP should not pay a
   quarter of a megabyte of flash for a stack it never calls. It needs
   board_build.tls = mbedtls-server. */
using WebServer = WebServerTemplate<EthernetServer>;
