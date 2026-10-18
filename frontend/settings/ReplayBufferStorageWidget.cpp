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

#include "ReplayBufferStorageWidget.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <util/platform.h>

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

static const int64_t DISK_SPACE_WARNING_MARGIN = 512LL * 1024 * 1024;

ReplayBufferStorageWidget::ReplayBufferStorageWidget(QWidget *parent) : QGroupBox(parent)
{
	setTitle(QTStr("Basic.Settings.Output.ReplayBuffer.Storage"));

	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(9, 6, 9, 9);

	auto modeRow = new QHBoxLayout;
	modeRow->setContentsMargins(0, 0, 0, 0);

	auto modeLabel = new QLabel(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Mode"), this);
	modeRow->addWidget(modeLabel);

	mode = new QComboBox(this);
	mode->setAccessibleName(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Mode"));
	mode->addItem(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Ram"), false);
	mode->addItem(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Disk"), true);
	modeRow->addWidget(mode);
	modeRow->addStretch(1);
	layout->addLayout(modeRow);

	pathRow = new QWidget(this);
	auto pathLayout = new QHBoxLayout(pathRow);
	pathLayout->setContentsMargins(0, 0, 0, 0);

	auto pathLabel = new QLabel(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Folder"), pathRow);
	pathLayout->addWidget(pathLabel);

	directory = new QLineEdit(pathRow);
	directory->setAccessibleName(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Folder"));
	directory->setText(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
	pathLayout->addWidget(directory, 1);

	browse = new QPushButton(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Browse"), pathRow);
	pathLayout->addWidget(browse);

	layout->addWidget(pathRow);

	hint = new QLabel(QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Hint"), this);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	summary = new QLabel(this);
	summary->setWordWrap(true);
	layout->addWidget(summary);

	warning = new QLabel(this);
	warning->setWordWrap(true);
	warning->setProperty("class", "text-warning");
	layout->addWidget(warning);

	connect(mode, &QComboBox::currentIndexChanged, this, [this](int) { OnModeChanged(); });
	connect(directory, &QLineEdit::textChanged, this, [this](const QString &) { OnDirectoryChanged(); });
	connect(browse, &QPushButton::clicked, this, &ReplayBufferStorageWidget::Browse);

	UpdateEnabled();
	RefreshSummary();
}

void ReplayBufferStorageWidget::SetDiskMode(bool diskMode)
{
	updating = true;
	QSignalBlocker blocker(mode);
	mode->setCurrentIndex(diskMode ? 1 : 0);
	updating = false;
	UpdateEnabled();
	RefreshSummary();
}

bool ReplayBufferStorageWidget::DiskMode() const
{
	return mode->currentIndex() == 1;
}

void ReplayBufferStorageWidget::SetDirectory(const QString &value)
{
	updating = true;
	QSignalBlocker blocker(directory);
	directory->setText(value);
	updating = false;
	RefreshSummary();
}

QString ReplayBufferStorageWidget::Directory() const
{
	return directory->text().trimmed();
}

void ReplayBufferStorageWidget::SetEstimate(int64_t bytes)
{
	estimateBytes = bytes;
	RefreshSummary();
}

void ReplayBufferStorageWidget::OnModeChanged()
{
	UpdateEnabled();
	RefreshSummary();
	if (!updating)
		emit Changed();
}

void ReplayBufferStorageWidget::OnDirectoryChanged()
{
	RefreshSummary();
	if (!updating)
		emit Changed();
}

void ReplayBufferStorageWidget::Browse()
{
	const QString selected = QFileDialog::getExistingDirectory(
		this, QTStr("Basic.Settings.Output.ReplayBuffer.Storage.SelectFolder"), Directory());
	if (!selected.isEmpty())
		directory->setText(selected);
}

void ReplayBufferStorageWidget::UpdateEnabled()
{
	const bool disk = DiskMode();
	pathRow->setVisible(disk);
	hint->setVisible(disk);
	summary->setVisible(disk);
	warning->setVisible(disk);
}

void ReplayBufferStorageWidget::RefreshSummary()
{
	if (!DiskMode()) {
		summary->clear();
		warning->clear();
		return;
	}

	const QString path = Directory();
	const int64_t freeBytes = path.isEmpty() ? -1 : os_get_free_space(QT_TO_UTF8(path));

	QString estimateText;
	if (estimateBytes < 0)
		estimateText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.EstimateUnknown");
	else
		estimateText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Estimate").arg(FormatBytes(estimateBytes));

	QString freeText;
	if (freeBytes < 0)
		freeText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.FreeUnknown");
	else
		freeText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Free").arg(FormatBytes(freeBytes));

	summary->setText(estimateText + QLatin1Char('\n') + freeText);

	QString warningText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.WearWarning");
	if (freeBytes >= 0 && estimateBytes >= 0 && estimateBytes + DISK_SPACE_WARNING_MARGIN > freeBytes) {
		warningText = QTStr("Basic.Settings.Output.ReplayBuffer.Storage.NotEnoughSpace") +
			      QLatin1Char('\n') + warningText;
	}
	warning->setText(warningText);
}

QString ReplayBufferStorageWidget::FormatBytes(int64_t bytes)
{
	if (bytes < 0)
		return QTStr("Basic.Settings.Output.ReplayBuffer.Storage.Unknown");

	const double megabytes = (double)bytes / (1024.0 * 1024.0);
	if (megabytes >= 1024.0)
		return QStringLiteral("%1 GB").arg(QString::number(megabytes / 1024.0, 'f', 1));
	return QStringLiteral("%1 MB").arg(QString::number(megabytes, 'f', 0));
}

#include "moc_ReplayBufferStorageWidget.cpp"
