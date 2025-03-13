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

#include "Statistics.h"

#include "Logger.h"
#include "Utilities.h"

Statistics::Statistics()
{
}

void Statistics::initialize()
{
    m_startTimeInNanoSeconds = m_lastReportTimeInNanoSeconds = getCurrentTimeInNanoSeconds();
}

void Statistics::reportFrameRate(bool force) const
{
    auto currentTimeInNanoSeconds = getCurrentTimeInNanoSeconds();
    if (currentTimeInNanoSeconds - m_lastReportTimeInNanoSeconds <= 2 * nsPerSecond && !force)
        return;

    double elapsedTimeInNanoSeconds = currentTimeInNanoSeconds - m_startTimeInNanoSeconds;
    double elapsedTimeInSeconds = elapsedTimeInNanoSeconds / double(nsPerSecond);

    if (elapsedTimeInSeconds <= 0) return; // Prevent division by zero

    auto frames = m_currentFrame - 1; // first frame ignored
    Logger::info("Rendered %5llu frames in %.3f sec (%.3f fps)\n",
		 static_cast<unsigned long long>(frames),
		 elapsedTimeInSeconds,
		 double(frames) / elapsedTimeInSeconds);
    m_lastReportTimeInNanoSeconds = currentTimeInNanoSeconds;
}
