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

#include "ReplayBufferDock.hpp"

#include "OBSBasic.hpp"

#include <utility/ReplayBufferDurations.hpp>

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ReplayBufferDock::ReplayBufferDock(OBSBasic *main_, QWidget *parent) : QWidget(parent), main(main_)
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	status = new QLabel(this);
	status->setWordWrap(true);
	layout->addWidget(status);

	durationsLabel = new QLabel(QTStr("Basic.ReplayBufferDock.Durations"), this);
	layout->addWidget(durationsLabel);

	buttonHost = new QWidget(this);
	buttonLayout = new QGridLayout(buttonHost);
	buttonLayout->setContentsMargins(0, 0, 0, 0);
	buttonLayout->setSpacing(4);
	layout->addWidget(buttonHost);

	empty = new QLabel(QTStr("Basic.ReplayBufferDock.NoDurations"), this);
	empty->setWordWrap(true);
	layout->addWidget(empty);

	fullButton = new QPushButton(QTStr("Basic.ReplayBufferDock.SaveFull"), this);
	fullButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	layout->addWidget(fullButton);

	auto hint = new QLabel(QTStr("Basic.ReplayBufferDock.Hint"), this);
	hint->setWordWrap(true);
	hint->setProperty("class", "text-muted");
	layout->addWidget(hint);

	layout->addStretch(1);

	connect(fullButton, &QPushButton::clicked, main, &OBSBasic::ReplayBufferSave);
	connect(main, &OBSBasic::ReplayBufStarted, this, &ReplayBufferDock::UpdateState);
	connect(main, &OBSBasic::ReplayBufStopping, this, &ReplayBufferDock::UpdateState);
	connect(main, &OBSBasic::ReplayBufStopped, this, &ReplayBufferDock::UpdateState);

	RefreshDurations();
}

void ReplayBufferDock::RefreshDurations()
{
	RebuildButtons();
	UpdateState();
}

void ReplayBufferDock::RebuildButtons()
{
	while (QLayoutItem *item = buttonLayout->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}
	durationButtons.clear();

	const std::vector<int> durations = main->GetReplayBufferDurations();
	int row = 0;
	int column = 0;

	for (int seconds : durations) {
		auto *button = new QPushButton(
			QTStr("Basic.ReplayBufferDock.SaveDuration").arg(ReplayBufferDurations::Format(seconds)),
			buttonHost);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		connect(button, &QPushButton::clicked, this,
			[this, seconds]() { main->ReplayBufferSaveDuration(seconds); });

		buttonLayout->addWidget(button, row, column);
		durationButtons.push_back(button);

		if (++column == 3) {
			column = 0;
			row++;
		}
	}

	const bool hasDurations = !durations.empty();
	durationsLabel->setVisible(hasDurations);
	buttonHost->setVisible(hasDurations);
	empty->setVisible(!hasDurations);
}

void ReplayBufferDock::UpdateState()
{
	const bool active = main->ReplayBufferActive();

	status->setText(QTStr(active ? "Basic.ReplayBufferDock.Status.Active"
				     : "Basic.ReplayBufferDock.Status.Inactive"));

	for (QPushButton *button : durationButtons)
		button->setEnabled(active);
	fullButton->setEnabled(active);
}

#include "moc_ReplayBufferDock.cpp"
