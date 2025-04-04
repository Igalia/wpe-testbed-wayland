/* wpe-testbed: WPE/WebKit painting/composition simulation
 *
 * Copyright (C) 2024 Igalia S.L.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <cstdarg>
#include <cstdio>

class Logger {
public:
    static void info(const char* format, ...)
    {
        va_list argptr;
        va_start(argptr, format);

        vfprintf(stdout, format, argptr);

        va_end(argptr);
    }

    static void error(const char* format, ...)
    {
        va_list argptr;
        va_start(argptr, format);

        fprintf(stderr, "*** ERROR: ");
        vfprintf(stderr, format, argptr);

        va_end(argptr);
    }
};

