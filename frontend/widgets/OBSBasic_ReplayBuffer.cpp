/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

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

#include "OBSBasic.hpp"
#include "ReplayBufferDock.hpp"

#include <components/UIValidation.hpp>
#include <docks/OBSDock.hpp>
#include <utility/ReplayBufferDurations.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

#define REPLAY_BUFFER_START "==== Replay Buffer Start ==========================================="
#define REPLAY_BUFFER_STOP "==== Replay Buffer Stop ============================================"

#define REPLAY_BUFFER_DURATION_HOTKEY_PREFIX "OBSBasic.SaveReplayBuffer."

static std::string MakeReplayBufferDurationHotkeyName(int seconds)
{
	return REPLAY_BUFFER_DURATION_HOTKEY_PREFIX + std::to_string(seconds);
}

static void ReplayBufferDurationHotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed) {
		return;
	}

	OBSBasic *basic = static_cast<OBSBasic *>(data);
	const char *name = obs_hotkey_get_name(hotkey);
	const size_t prefix_len = sizeof(REPLAY_BUFFER_DURATION_HOTKEY_PREFIX) - 1;

	if (!name || strncmp(name, REPLAY_BUFFER_DURATION_HOTKEY_PREFIX, prefix_len) != 0) {
		return;
	}

	const char *value = name + prefix_len;
	char *end = nullptr;
	long seconds = strtol(value, &end, 10);

	if (end == value || *end != '\0' || seconds <= 0 || seconds > 24 * 60 * 60) {
		return;
	}

	basic->ReplayBufferSaveDuration((int)seconds);
}

std::vector<int> OBSBasic::ConfiguredReplayBufferDurations() const
{
	const char *mode = config_get_string(activeConfiguration, "Output", "Mode");
	const char *section = (mode && strcmp(mode, "Advanced") == 0) ? "AdvOut" : "SimpleOutput";
	const char *csv = config_get_string(activeConfiguration, section, "RecRBExtraDurations");

	return ReplayBufferDurations::Parse(QString::fromUtf8(csv ? csv : ""));
}

std::vector<int> OBSBasic::GetReplayBufferDurations() const
{
	return ConfiguredReplayBufferDurations();
}

void OBSBasic::CreateReplayBufferDock()
{
	if (replayBufferDock)
		return;

	auto *dock = new OBSDock(this);
	dock->setObjectName(QStringLiteral("replayBufferDock"));
	dock->setWindowTitle(QTStr("ReplayBuffer"));

	auto *widget = new ReplayBufferDock(this, dock);
	dock->setWidget(widget);

	AddDockWidget(dock, Qt::RightDockWidgetArea);
	dock->setVisible(false);
	dock->setFloating(true);
	dock->resize(320, 300);

	replayBufferDock = dock;
	replayBufferDockWidget = widget;
}

void OBSBasic::RefreshReplayBufferDock()
{
	if (replayBufferDockWidget)
		replayBufferDockWidget->RefreshDurations();
}

void OBSBasic::CreateReplayBufferDurationHotkeys()
{
	for (int seconds : ConfiguredReplayBufferDurations()) {
		std::string name = MakeReplayBufferDurationHotkeyName(seconds);
		const char *json = config_get_string(activeConfiguration, "Hotkeys", name.c_str());

		OBSDataAutoRelease data;
		OBSDataArrayAutoRelease bindings;
		if (json && *json) {
			data = obs_data_create_from_json(json);
			if (data) {
				bindings = obs_data_get_array(data, "bindings");
			}
		}

		obs_hotkey_id id = obs_hotkey_register_frontend(
			name.c_str(), QT_TO_UTF8(ReplayBufferDurations::HotkeyDescription(seconds)),
			ReplayBufferDurationHotkeyCallback, this);

		if (bindings) {
			obs_hotkey_load(id, bindings);
		}

		replayBufferDurationHotkeys.push_back({seconds, id, std::move(name)});
	}
}

bool OBSBasic::RefreshReplayBufferDurationHotkeys()
{
	const std::vector<int> configured = ConfiguredReplayBufferDurations();

	if (configured.size() == replayBufferDurationHotkeys.size() &&
	    std::equal(configured.begin(), configured.end(), replayBufferDurationHotkeys.begin(),
		       [](int seconds, const ReplayBufferDurationHotkey &hotkey) {
			       return seconds == hotkey.seconds;
		       })) {
		return false;
	}

	/*
	 * Persist current bindings before unregistering.  Duration hotkeys are
	 * frontend hotkeys, so they live in the profile's [Hotkeys] section just
	 * like the existing "Save Replay Buffer" hotkey.
	 */
	for (const ReplayBufferDurationHotkey &hotkey : replayBufferDurationHotkeys) {
		OBSDataArrayAutoRelease array = obs_hotkey_save(hotkey.id);
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_array(data, "bindings", array);
		config_set_string(activeConfiguration, "Hotkeys", hotkey.name.c_str(), obs_data_get_json(data));
		obs_hotkey_unregister(hotkey.id);
	}
	replayBufferDurationHotkeys.clear();

	CreateReplayBufferDurationHotkeys();
	RefreshReplayBufferDock();
	return true;
}

void OBSBasic::ReplayBufferActionTriggered()
{
	if (outputHandler->ReplayBufferActive()) {
		StopReplayBuffer();
	} else {
		StartReplayBuffer();
	}
};

void OBSBasic::ShowReplayBufferPauseWarning()
{
	auto msgBox = []() {
		QMessageBox msgbox(App()->GetMainWindow());
		msgbox.setWindowTitle(QTStr("Output.ReplayBuffer."
					    "PauseWarning.Title"));
		msgbox.setText(QTStr("Output.ReplayBuffer."
				     "PauseWarning.Text"));
		msgbox.setIcon(QMessageBox::Icon::Information);
		msgbox.addButton(QMessageBox::Ok);

		QCheckBox *cb = new QCheckBox(QTStr("DoNotShowAgain"));
		msgbox.setCheckBox(cb);

		msgbox.exec();

		if (cb->isChecked()) {
			config_set_bool(App()->GetUserConfig(), "General", "WarnedAboutReplayBufferPausing", true);
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	};

	bool warned = config_get_bool(App()->GetUserConfig(), "General", "WarnedAboutReplayBufferPausing");
	if (!warned) {
		QMetaObject::invokeMethod(App(), &OBSApp::Exec, Qt::QueuedConnection, msgBox);
	}
}

void OBSBasic::StartReplayBuffer()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	if (outputHandler->ReplayBufferActive()) {
		return;
	}
	if (disableOutputsRef) {
		return;
	}

	if (!UIValidation::NoSourcesConfirmation(this)) {
		return;
	}

	if (!OutputPathValid()) {
		OutputPathInvalidMessage();
		return;
	}

	if (LowDiskSpace()) {
		DiskSpaceMessage();
		return;
	}

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING);

	SaveProject();

	if (outputHandler->StartReplayBuffer() && os_atomic_load_bool(&recording_paused)) {
		ShowReplayBufferPauseWarning();
	}
}

void OBSBasic::ReplayBufferStopping()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStopping();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StoppingReplayBuffer"));
	}

	replayBufferStopping = true;
	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING);
}

void OBSBasic::StopReplayBuffer()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	SaveProject();

	if (outputHandler->ReplayBufferActive()) {
		outputHandler->StopReplayBuffer(replayBufferStopping);
	}

	OnDeactivate();
}

void OBSBasic::ReplayBufferStart()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStarted();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StopReplayBuffer"));
	}

	replayBufferStopping = false;
	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED);

	OnActivate();

	blog(LOG_INFO, REPLAY_BUFFER_START);
}

void OBSBasic::ReplayBufferSave()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	if (!outputHandler->ReplayBufferActive()) {
		return;
	}

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVING);

	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(outputHandler->replayBuffer);
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
}

void OBSBasic::ReplayBufferSaveDuration(int seconds)
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	if (!outputHandler->ReplayBufferActive()) {
		return;
	}
	if (seconds <= 0) {
		return;
	}

	blog(LOG_INFO, "Saving replay buffer (%d seconds) due to hotkey", seconds);
	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVING);

	calldata_t cd = {0};
	calldata_set_int(&cd, "seconds", seconds);
	proc_handler_t *ph = obs_output_get_proc_handler(outputHandler->replayBuffer);
	proc_handler_call(ph, "save_duration", &cd);
	calldata_free(&cd);
}

void OBSBasic::ReplayBufferSaveFailed(const QString &reason)
{
	QString msg = QTStr("Basic.StatusBar.ReplayBufferSaveFailed").arg(reason);
	ShowStatusBarMessage(msg);
}

void OBSBasic::ReplayBufferFailed(const QString &reason)
{
	QString msg = QTStr("Basic.StatusBar.ReplayBufferFailed").arg(reason);
	ShowStatusBarMessage(msg);
}

void OBSBasic::ReplayBufferSaved()
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}
	if (!outputHandler->ReplayBufferActive()) {
		return;
	}

	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(outputHandler->replayBuffer);
	proc_handler_call(ph, "get_last_replay", &cd);
	std::string path = calldata_string(&cd, "path");
	QString msg = QTStr("Basic.StatusBar.ReplayBufferSavedTo").arg(QT_UTF8(path.c_str()));
	ShowStatusBarMessage(msg);
	lastReplay = path;
	calldata_free(&cd);

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED);

	AutoRemux(QT_UTF8(path.c_str()));
}

void OBSBasic::ReplayBufferStop(int code)
{
	if (!outputHandler || !outputHandler->replayBuffer) {
		return;
	}

	emit ReplayBufStopped();

	if (sysTrayReplayBuffer) {
		sysTrayReplayBuffer->setText(QTStr("Basic.Main.StartReplayBuffer"));
	}

	blog(LOG_INFO, REPLAY_BUFFER_STOP);

	if (code == OBS_OUTPUT_UNSUPPORTED && isVisible()) {
		OBSMessageBox::critical(this, QTStr("Output.RecordFail.Title"), QTStr("Output.RecordFail.Unsupported"));

	} else if (code == OBS_OUTPUT_NO_SPACE && isVisible()) {
		OBSMessageBox::warning(this, QTStr("Output.RecordNoSpace.Title"), QTStr("Output.RecordNoSpace.Msg"));

	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {
		OBSMessageBox::critical(this, QTStr("Output.RecordError.Title"), QTStr("Output.RecordError.Msg"));

	} else if (code == OBS_OUTPUT_UNSUPPORTED && !isVisible()) {
		SysTrayNotify(QTStr("Output.RecordFail.Unsupported"), QSystemTrayIcon::Warning);

	} else if (code == OBS_OUTPUT_NO_SPACE && !isVisible()) {
		SysTrayNotify(QTStr("Output.RecordNoSpace.Msg"), QSystemTrayIcon::Warning);

	} else if (code != OBS_OUTPUT_SUCCESS && !isVisible()) {
		SysTrayNotify(QTStr("Output.RecordError.Msg"), QSystemTrayIcon::Warning);
	}

	OnEvent(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED);

	OnDeactivate();
}

bool OBSBasic::ReplayBufferActive()
{
	if (!outputHandler) {
		return false;
	}
	return outputHandler->ReplayBufferActive();
}
