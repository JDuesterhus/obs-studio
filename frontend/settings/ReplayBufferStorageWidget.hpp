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

#include <cstdint>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/*
 * Selects whether the replay buffer lives in RAM or in a rolling set of files
 * on disk.  The same widget is used in simple and advanced output mode.
 */
class ReplayBufferStorageWidget : public QGroupBox {
	Q_OBJECT;

public:
	explicit ReplayBufferStorageWidget(QWidget *parent = nullptr);

	void SetDiskMode(bool diskMode);
	bool DiskMode() const;

	void SetDirectory(const QString &directory);
	QString Directory() const;

	/* Estimated bytes for the configured buffer length, or -1 if unknown. */
	void SetEstimate(int64_t bytes);

signals:
	void Changed();

private:
	void OnModeChanged();
	void OnDirectoryChanged();
	void Browse();
	void UpdateEnabled();
	void RefreshSummary();

	static QString FormatBytes(int64_t bytes);

	QComboBox *mode;
	QWidget *pathRow;
	QLineEdit *directory;
	QPushButton *browse;
	QLabel *hint;
	QLabel *summary;
	QLabel *warning;

	int64_t estimateBytes = -1;
	bool updating = false;
};
