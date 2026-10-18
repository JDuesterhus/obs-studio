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

#include "ReplayBufferDurations.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QStringList>

#include <algorithm>

namespace ReplayBufferDurations {

std::vector<int> Parse(const QString &csv)
{
	std::vector<int> durations;

	for (const QString &part : csv.split(',', Qt::SkipEmptyParts)) {
		bool ok = false;
		int seconds = part.trimmed().toInt(&ok);

		/* Keep values in the same range as the settings spin box. */
		if (!ok || seconds < 1 || seconds > 24 * 60 * 60) {
			continue;
		}

		if (std::find(durations.begin(), durations.end(), seconds) == durations.end()) {
			durations.push_back(seconds);
		}
	}

	std::sort(durations.begin(), durations.end());
	return durations;
}

QString Serialize(const std::vector<int> &durations)
{
	QStringList parts;
	for (int seconds : durations) {
		parts << QString::number(seconds);
	}
	return parts.join(',');
}

QString Format(int seconds)
{
	QStringList parts;
	int remaining = seconds;

	const int hours = remaining / 3600;
	remaining %= 3600;
	const int minutes = remaining / 60;
	const int secs = remaining % 60;

	if (hours) {
		parts << QTStr(hours == 1 ? "Basic.Settings.Output.ReplayBuffer.Duration.Hour"
					  : "Basic.Settings.Output.ReplayBuffer.Duration.Hours")
				 .arg(hours);
	}
	if (minutes) {
		parts << QTStr(minutes == 1 ? "Basic.Settings.Output.ReplayBuffer.Duration.Minute"
					    : "Basic.Settings.Output.ReplayBuffer.Duration.Minutes")
				 .arg(minutes);
	}
	if (secs || parts.isEmpty()) {
		parts << QTStr(secs == 1 ? "Basic.Settings.Output.ReplayBuffer.Duration.Second"
					 : "Basic.Settings.Output.ReplayBuffer.Duration.Seconds")
				 .arg(secs);
	}

	return parts.join(' ');
}

QString HotkeyDescription(int seconds)
{
	return QTStr("Basic.Main.SaveReplayForDuration").arg(Format(seconds));
}

} // namespace ReplayBufferDurations
