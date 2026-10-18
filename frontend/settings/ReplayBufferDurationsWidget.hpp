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

#include <QGroupBox>
#include <QString>
#include <vector>

class QListWidget;
class QPushButton;
class QSpinBox;

/*
 * Editor for the additional replay buffer durations.  The same widget is used
 * in simple and advanced output mode, which keeps both UIs identical and the
 * settings code small.
 */
class ReplayBufferDurationsWidget : public QGroupBox {
	Q_OBJECT;

public:
	explicit ReplayBufferDurationsWidget(QWidget *parent = nullptr);

	void SetDurations(const QString &csv);
	std::vector<int> Durations() const;
	QString DurationsString() const;
	int LongestDuration() const;

signals:
	void Changed();

private:
	void RefreshList();
	void AddCurrentDuration();
	void RemoveSelectedDuration();

	QListWidget *list;
	QSpinBox *seconds;
	QPushButton *addButton;
	QPushButton *removeButton;
	std::vector<int> durations;
};
