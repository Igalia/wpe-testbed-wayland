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

#include <cstdint>

class alignas(8) Statistics {
public:
    Statistics();

    void initialize();
    void reportFrameRate(bool force = false) const;

    void advanceFrame() { ++m_currentFrame; }
    uint64_t currentFrame() const { return m_currentFrame; }

private:
    alignas(8) uint64_t m_currentFrame { 0 };
    alignas(8) int64_t m_startTimeInNanoSeconds { 0 };
    alignas(8) mutable int64_t m_lastReportTimeInNanoSeconds { 0 };
};
