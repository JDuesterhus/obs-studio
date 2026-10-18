> ⚠️ **Disclaimer:** This is vibecoded slop and may become unmaintained at any
> time. Use it at your own risk. For important work, use official OBS Studio.

# OBS Studio: Multiple Replay Buffer Durations

This repository is a fork of [OBS Studio](https://github.com/obsproject/obs-studio)
that adds **several Replay Buffer durations, each with its own hotkey**, to the
current OBS codebase. It is meant to be used like normal OBS; there is no
external plugin, helper application, or hand-edited configuration file.

The feature was requested by the OBS community:
<https://ideas.obsproject.com/posts/263/multiple-replay-buffer-hotkeys-with-different-time-length>

This fork is based on OBS Studio 33.x. It is **not** an official OBS Project
release and is not affiliated with or endorsed by the OBS Project.

## What this fork adds

- Any number of extra Replay Buffer durations (for example 30 seconds,
  2 minutes, 10 minutes, and 1 hour).
- One configurable OBS hotkey per duration, managed in **Settings > Hotkeys**
  like every other OBS hotkey.
- A duration editor directly in **Settings > Output**, in both Simple and
  Advanced output mode.
- Hotkeys are stored per profile and survive OBS restarts.
- Existing Replay Buffer behavior is preserved: the normal **Save Replay**
  hotkey still saves the whole buffered clip.
- Clips are cut from the in-memory encoded packet buffer and muxed directly.
  There is **no re-encode, no temporary full-size file, and no second trimming
  pass**.
- Clips start on a video keyframe, so the first frame is decodable.
- MP4/MOV clips report their real, shorter length in Windows Explorer even when
  the requested duration is longer than the footage that is currently buffered.
- If less footage is available than requested, the oldest available keyframe is
  used and the clip is saved with whatever exists instead of failing.
- The first save after startup is reliable. Older forks could produce a tiny,
  unplayable first file because a stale "requested length" value was left at
  zero; this fork never carries a request over and explicitly treats zero as
  "save the whole buffer".

## Differences from earlier implementations

- [TheDark/obs-studio](https://github.com/TheDark/obs-studio) is based on an old
  OBS version and registers duration hotkeys on the output. This fork uses
  frontend hotkeys, so bindings are saved with the profile and appear in the
  normal hotkey settings page. Its cut also did not align to keyframes.
- [replay-buffer-pro](https://github.com/JoshuaPotter/replay-buffer-pro) saves
  the full buffer and then trims the file with FFmpeg stream copy. That works,
  but it needs a second pass over a potentially very large file. This fork
  selects the requested packet window before muxing, so shorter clips are
  cheaper to save and their container duration is written correctly from the
  start.

## Configuring durations

1. Open **Settings > Output**.
2. Select **Simple** or **Advanced** output mode.
3. Enable **Replay Buffer** and set **Maximum Replay Time** to at least the
   longest duration you want. If you add a longer duration, OBS raises this
   value automatically.
4. In the **Replay Buffer Durations** box, enter a duration in seconds and
   click **Add duration**. Repeat for every length you want.
5. Click **Apply**.
6. Open **Settings > Hotkeys** and assign a key to each
   **Save Replay Buffer (N seconds)** entry.

The existing **Save Replay** hotkey is separate and still saves the complete
buffer. Duration hotkeys are identified by their length: if you change a
duration from 30 to 45 seconds, assign the hotkey for 45 seconds again.

The Replay Buffer must be stopped while changing its settings, just like the
existing OBS replay buffer options. Start it again after applying.

## Using it

- Start the Replay Buffer as usual.
- Press a duration hotkey to save the last N seconds.
- Press the normal Save Replay hotkey to save the whole buffered clip.
- The status bar shows the saved file, and the existing Auto Remux option is
  honored.

A clip may be slightly longer than requested because it starts at the nearest
keyframe at or before the requested start point. The extra time is at most one
keyframe interval. A two-second keyframe interval therefore adds at most about
two seconds; a longer interval (OBS's simple output can default to several
seconds) adds more pre-roll. This is intentional: cutting inside a GOP would
produce a clip whose first frames cannot be decoded.

If the buffer has not received a video keyframe yet (usually only for a fraction
of a second after starting), OBS shows a status-bar warning instead of writing
an empty or corrupt file. If muxing itself fails (for example the helper process
cannot start or the disk is full), the partial file is deleted and the failure
is reported in the status bar. Existing replay files are never overwritten; a
colliding filename gets OBS's usual ` (2)` suffix.

## Windows installation

The GitHub Actions workflow produces a **ZIP archive, not a setup `.exe`**. It
is the same kind of ZIP that the normal OBS Windows build produces.

1. Download the latest `obs-studio-*-windows-x64.zip` from the repository's
   **Releases** page.
2. Extract it to a folder of your choice. Do not run it from inside the ZIP.
3. Start `bin\64bit\obs64.exe`.

Inside the extracted folder:

- `bin\64bit\obs64.exe` is the program you start.
- `core\` and `data\` contain the OBS modules and data files. Leave them next
  to `bin\`.
- The ZIP is relocatable; you can move the extracted folder anywhere.

The build is not code-signed. Windows SmartScreen may warn about an unknown
publisher; choose **More info > Run anyway** only if you trust the download.
Windows 10 21H2 or newer is recommended. It uses your normal OBS configuration
folder (`%APPDATA%\obs-studio`) by default. For a fully portable install, create
an empty file named `portable_mode` inside `bin\64bit` (next to `obs64.exe`);
OBS then keeps its configuration in the `config` folder at the root of the
extracted archive.

## Building from source

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 or newer with the C++ desktop workload
- CMake 3.28 or newer
- Git, with submodules checked out

```powershell
git clone --recurse-submodules https://github.com/JDuesterhus/obs-studio.git
cd obs-studio

cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo --target obs-studio
```

The build downloads the OBS dependency bundle and Qt automatically. The
resulting application is in `build_x64/rundir/RelWithDebInfo/bin/64bit`.

### Automated tests

The keyframe-window selection logic has a dependency-free unit test:

```powershell
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 -DENABLE_REPLAY_BUFFER_TESTS=ON
cmake --build build_x64 --config RelWithDebInfo --target test_replay_buffer_window
ctest --test-dir build_x64 -C RelWithDebInfo -R test_replay_buffer_window --output-on-failure
```

The GitHub Actions workflow runs this test and builds the Windows x64 package.

## Verification performed

The implementation was built and tested on Windows with real OBS runs, not only
compiled:

| Test | Result |
| --- | --- |
| Several durations saved in one session (5, 10, 20 seconds) | Passed |
| Full-buffer Save Replay hotkey | Passed |
| First save after startup using a duration hotkey | Passed (valid 6.4 s clip) |
| Requested 20 s from an approximately 12 s buffer | Passed (11.3 s saved) |
| Repeated saves in a row | Passed |
| MP4 / MOV duration shown in Windows Explorer | Passed (matches ffprobe) |
| MP4 + H.264 + AAC | Passed |
| MKV + H.264 + AAC | Passed |
| FLV + H.264 + AAC | Passed |
| MOV + H.264 + AAC | Passed |
| MP4 + NVENC HEVC + AAC | Passed |
| MP4 + SVT-AV1 + AAC | Passed |
| Add/remove durations through the Settings UI, then apply | Passed |
| Hotkey list updates after applying new durations | Passed |
| Hotkey bindings survive an OBS restart | Passed |
| Legacy profile with no extra durations configured | Passed |
| Two saves with the same generated filename | Passed: distinct files, no overwrite |
| Mux-helper failure (helper removed) | Passed: warning shown, no partial file left |
| Unit test for keyframe window selection | Passed |

Example measurements from the integration runs:

| Requested | Actual clip | Container |
| --- | --- | --- |
| Whole buffer | 27.9 s | MP4, Explorer: 00:00:27 |
| 5 s | 6.4 s | MP4, Explorer: 00:00:06 |
| 10 s | 10.1 s | MP4, Explorer: 00:00:10 |
| 20 s | 21.2 s | MP4, Explorer: 00:00:21 |
| 20 s from a 12 s buffer | 11.3 s | MP4 |

Not tested yet:

- Other audio codecs (Opus, PCM, FLAC) with the replay buffer.
- Hardware encoders other than NVENC HEVC.
- Linux and macOS builds. The changes are platform-independent, but only the
  Windows build was compiled and exercised here.
- Pausing the buffer, very long (multi-hour) buffers, and disk-full conditions.
- Code signing / SmartScreen reputation for the released ZIP.

## Upstream maintenance

The feature is deliberately kept in a small number of places:

- `plugins/obs-ffmpeg/obs-ffmpeg-mux.c` and `replay-buffer-window.h`:
  duration-aware saving and keyframe selection.
- `frontend/utility/SimpleOutput.cpp`, `AdvancedOutput.cpp`:
  pass configured durations to the replay output.
- `frontend/widgets/OBSBasic_ReplayBuffer.cpp`, `OBSBasic_Hotkeys.cpp`:
  duration hotkeys and saving.
- `frontend/settings/ReplayBufferDurationsWidget.*`,
  `frontend/utility/ReplayBufferDurations.*`:
  settings UI and shared formatting/parsing.
- `frontend/settings/OBSBasicSettings.*` and locale files:
  wiring the duration editor into both output modes.
- `test/replay-buffer/`: unit test for the packet-window logic.

## License

This fork is distributed under the same GNU General Public License v2 or later
as OBS Studio. See `COPYING`.
