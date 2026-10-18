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

#include "ReplayBufferDurationsWidget.hpp"

#include <utility/ReplayBufferDurations.hpp>

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

ReplayBufferDurationsWidget::ReplayBufferDurationsWidget(QWidget *parent) : QGroupBox(parent)
{
	setTitle(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations"));

	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(9, 6, 9, 9);

	auto hint = new QLabel(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Hint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	list = new QListWidget(this);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	list->setMinimumHeight(80);
	layout->addWidget(list);

	auto inputLayout = new QHBoxLayout;
	inputLayout->setContentsMargins(0, 0, 0, 0);

	auto label = new QLabel(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Duration"));
	inputLayout->addWidget(label);

	seconds = new QSpinBox(this);
	seconds->setAccessibleName(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Duration"));
	seconds->setRange(1, 24 * 60 * 60);
	seconds->setValue(30);
	seconds->setSuffix(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.SecondsSuffix"));
	inputLayout->addWidget(seconds);

	addButton = new QPushButton(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Add"), this);
	inputLayout->addWidget(addButton);

	removeButton = new QPushButton(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Remove"), this);
	inputLayout->addWidget(removeButton);

	inputLayout->addStretch(1);
	layout->addLayout(inputLayout);

	connect(addButton, &QPushButton::clicked, this, &ReplayBufferDurationsWidget::AddCurrentDuration);
	connect(removeButton, &QPushButton::clicked, this, &ReplayBufferDurationsWidget::RemoveSelectedDuration);
	connect(list, &QListWidget::itemSelectionChanged, this,
		[this]() { removeButton->setEnabled(list->currentItem() != nullptr); });
	connect(seconds, &QSpinBox::valueChanged, this, [this](int) { addButton->setEnabled(true); });

	removeButton->setEnabled(false);
	RefreshList();
}

void ReplayBufferDurationsWidget::SetDurations(const QString &csv)
{
	durations = ReplayBufferDurations::Parse(csv);
	RefreshList();
}

std::vector<int> ReplayBufferDurationsWidget::Durations() const
{
	return durations;
}

QString ReplayBufferDurationsWidget::DurationsString() const
{
	return ReplayBufferDurations::Serialize(durations);
}

int ReplayBufferDurationsWidget::LongestDuration() const
{
	return durations.empty() ? 0 : durations.back();
}

void ReplayBufferDurationsWidget::RefreshList()
{
	list->clear();

	if (durations.empty()) {
		auto item = new QListWidgetItem(QTStr("Basic.Settings.Output.ReplayBuffer.ExtraDurations.Empty"));
		item->setFlags(Qt::NoItemFlags);
		list->addItem(item);
	} else {
		for (int duration : durations) {
			auto item = new QListWidgetItem(ReplayBufferDurations::Format(duration));
			item->setData(Qt::UserRole, duration);
			list->addItem(item);
		}
	}

	removeButton->setEnabled(list->currentItem() != nullptr);
}

void ReplayBufferDurationsWidget::AddCurrentDuration()
{
	const int value = seconds->value();

	auto it = std::find(durations.begin(), durations.end(), value);
	if (it != durations.end()) {
		/* Already present: select it instead of adding a duplicate. */
		for (int row = 0; row < list->count(); row++) {
			if (list->item(row)->data(Qt::UserRole).toInt() == value) {
				list->setCurrentRow(row);
				break;
			}
		}
		return;
	}

	durations.push_back(value);
	std::sort(durations.begin(), durations.end());
	RefreshList();
	emit Changed();
}

void ReplayBufferDurationsWidget::RemoveSelectedDuration()
{
	auto *item = list->currentItem();
	if (!item) {
		return;
	}

	const int value = item->data(Qt::UserRole).toInt();
	auto it = std::find(durations.begin(), durations.end(), value);
	if (it == durations.end()) {
		return;
	}

	durations.erase(it);
	RefreshList();
	emit Changed();
}

#include "moc_ReplayBufferDurationsWidget.cpp"
