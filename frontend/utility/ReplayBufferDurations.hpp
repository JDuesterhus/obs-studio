/******************************************************************************
    Copyright (C) 2026 by Jeremy Düsterhus

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include <QString>
#include <vector>

/*
 * Helpers shared by the output settings UI and the duration hotkeys.  The
 * configuration stores durations as a comma separated list of seconds so it
 * stays readable and is easy to edit by hand.
 */
namespace ReplayBufferDurations {

std::vector<int> Parse(const QString &csv);
QString Serialize(const std::vector<int> &durations);
QString Format(int seconds);
QString HotkeyDescription(int seconds);

} // namespace ReplayBufferDurations
