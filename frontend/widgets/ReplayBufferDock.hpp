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

#include <QWidget>
#include <vector>

class OBSBasic;
class QGridLayout;
class QLabel;
class QPushButton;

/*
 * Dockable quick-save controls for the replay buffer.  The dock is created
 * hidden and can be shown from OBS's View > Docks menu.  It always mirrors the
 * durations configured in Settings > Output.
 */
class ReplayBufferDock : public QWidget {
	Q_OBJECT;

public:
	explicit ReplayBufferDock(OBSBasic *main, QWidget *parent = nullptr);

	void RefreshDurations();

public slots:
	void UpdateState();

private:
	void RebuildButtons();

	OBSBasic *main;
	QLabel *status = nullptr;
	QLabel *durationsLabel = nullptr;
	QLabel *empty = nullptr;
	QWidget *buttonHost = nullptr;
	QGridLayout *buttonLayout = nullptr;
	QPushButton *fullButton = nullptr;
	std::vector<QPushButton *> durationButtons;
};
