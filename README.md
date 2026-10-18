> ⚠️ **Disclaimer:** This is vibecoded slop and may become unmaintained at any
> time. Use it at your own risk. For important work, use official OBS Studio.

# OBS Studio: Multiple Replay Buffer Durations and Disk-Backed Buffer

This repository is a fork of [OBS Studio](https://github.com/obsproject/obs-studio)
that adds **several Replay Buffer durations, each with its own hotkey**, and an
optional **disk-backed Replay Buffer** that keeps the rolling buffer on an SSD
or HDD instead of in RAM. It is meant to be used like normal OBS; there is no
external plugin, helper application, or hand-edited configuration file.

The duration feature was requested by the OBS community:
<https://ideas.obsproject.com/posts/263/multiple-replay-buffer-hotkeys-with-different-time-length>

The disk-backed mode implements this request:
<https://ideas.obsproject.com/posts/777/replay-buffer-to-an-ssd-hdd-instead-of-ram>

This fork is based on OBS Studio 33.x. It is **not** an official OBS Project
release and is not affiliated with or endorsed by the OBS Project.

Downloads: <https://github.com/JDuesterhus/obs-studio/releases/latest>

## What this fork adds

- Any number of extra Replay Buffer durations (for example 30 seconds,
  2 minutes, 10 minutes, and 1 hour).
- One configurable OBS hotkey per duration, managed in **Settings > Hotkeys**
  like every other OBS hotkey.
- A duration editor directly in **Settings > Output**, in both Simple and
  Advanced output mode.
- An optional **Replay Buffer** dock with one save button per duration and a
  full-buffer button. It is hidden by default and can be opened from
  **View > Docks**.
- A selectable **RAM** or **disk** storage mode in **Settings > Output**.
  RAM remains the default.
- In disk mode, encoded packets are appended to rolling temporary segment
  files. RAM use stays bounded by a fixed write queue instead of growing with
  the configured buffer length.
- Disk mode shows an estimated buffer size, the free space on the selected
  drive, and a warning about continuous writes and SSD wear.
- Hotkeys, the dock, keyframe-aligned cuts, and the existing OBS muxers work
  the same in both storage modes.
- The duration and dock strings use the existing OBS terminology in the major
  OBS languages (for example German uses **Replaypuffer**, not a newly invented
  term). The new disk-storage strings are available in German and English;
  other locales fall back to English until native speakers review them.
- Existing Replay Buffer behavior is preserved: the normal **Save Replay**
  hotkey still saves the whole buffered clip.
- Clips are cut from the encoded packet stream (RAM or disk) and muxed
  directly. There is **no re-encode, no temporary full-size file, and no second
  trimming pass**.
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

## How disk-backed buffering works

Disk mode does not write a normal video file and it does not re-encode
anything. While the Replay Buffer runs, the output's already-encoded packets
are appended to a small set of temporary segment files inside a per-session
folder under the selected buffer folder:

```text
<buffer folder>/obs-replay-buffer-<session>/segment-000000.obsrb
```

- A background writer thread performs the disk writes. The encoder thread only
  queues encoded packets and is never asked to wait for normal disk I/O.
- The queue is capped at 64 MB. If the drive cannot keep up and the queue would
  exceed that limit, the Replay Buffer stops with a clear error instead of
  growing memory without bound.
- Segments roll both by size and by time. The newest part of the buffer is
  retained for the configured maximum replay time plus one segment, so the
  longest configured duration normally has a keyframe available.
- A save creates a pinned snapshot of the needed segments. The helper muxer
  reads those records and stream-copies them into the final clip with the same
  keyframe and timestamp handling as the RAM path. A save can therefore run
  while the buffer keeps recording, and it never re-encodes.
- Only encoded packets and a small keyframe index are kept in memory. The
  index grows with the keyframe interval, not with the media bitrate.
- The temporary folder is deleted when the Replay Buffer stops. If OBS crashes,
  a later session removes only folders matching the `obs-replay-buffer-*`
  prefix whose lock file is older than 24 hours, and only the segment/lock
  files inside them; unrelated files and recordings are left alone.

The disk buffer is roughly bounded by the configured duration (plus one
segment for keyframe alignment), not by a separate size limit. Estimate
roughly:

```text
video bitrate + audio bitrate (Mbit/s) x buffer seconds / 8 = disk use in GB
```

For example, 20 Mbit/s for 10 minutes is about 1.5 GB. Keep at least 512 MB of
free space on the target volume; OBS stops the buffer with a "disk full"
message when free space falls below that reserve. The folder must be writable
by the user running OBS. Network shares and removable drives work if they are
fast enough, but the writer will stop the buffer rather than let it block or
grow without bound.

Continuous writes can increase SSD wear. A dedicated SSD or a normal HDD is
fine for occasional clips; choose a drive and buffer length that match your
usage. The temporary files are not your recordings and are removed
automatically.

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

## Configuring disk-backed buffering

1. Open **Settings > Output**.
2. Under **Replay Buffer Storage**, set **Storage mode** to **Disk**.
3. Choose a **Buffer folder**. The normal recording folder is a sensible
   default; a fast local drive is recommended.
4. Apply the settings and start the Replay Buffer.

The storage box shows an estimated disk requirement and the free space in the
selected folder. A warning is shown when the estimate plus a safety margin is
larger than the available space. The normal **Maximum Memory** field is hidden
in disk mode because the buffer no longer consumes RAM for its duration.

Switch back to **RAM (default)** at any time to restore the previous in-memory
behavior. Existing profiles keep RAM mode unless disk mode is explicitly
selected.

## Using it

- Start the Replay Buffer as usual.
- Press a duration hotkey to save the last N seconds.
- Press the normal Save Replay hotkey to save the whole buffered clip.
- The status bar shows the saved file, and the existing Auto Remux option is
  honored.
- In disk mode, saving starts a separate read/mux pass over the temporary
  segments. The buffer keeps running, and the resulting clip is written to the
  normal recording folder.

A clip may be slightly longer than requested because it starts at the nearest
keyframe at or before the requested start point. The extra time is at most one
keyframe interval. A two-second keyframe interval therefore adds at most about
two seconds; a longer interval (OBS's simple output can default to several
seconds) adds more pre-roll. This is intentional: cutting inside a GOP would
produce a clip whose first frames cannot be decoded.

A whole-buffer save can likewise include up to one disk segment of extra
footage. Segments are capped at 60 seconds, or a quarter of the configured
window for short buffers, so a 30-second buffer may save up to about 37 seconds.
A duration-specific save still targets the requested length and normally lands
within one keyframe interval above it.

If the buffer has not received a video keyframe yet (usually only for a fraction
of a second after starting), OBS shows a status-bar warning instead of writing
an empty or corrupt file. If muxing itself fails (for example the helper process
cannot start or the disk is full), the temporary file is deleted and the
failure is reported in the status bar. A save is written to a `.part` file
first and renamed to its final name only after the muxer finishes, so an
interrupted save cannot look like a complete clip. Existing replay files are
never overwritten; a colliding filename gets OBS's usual ` (2)` suffix.

## Replay Buffer dock

The fork adds a native OBS dock, hidden by default:

1. Open **View > Docks > Replay Buffer**.
2. While the Replay Buffer is running, click **Save N seconds** for any
   configured duration, or **Save full buffer**.
3. Close the dock like any other OBS dock; OBS remembers its position and
   visibility.

The dock mirrors **Settings > Output**. Adding or removing a duration updates
the dock immediately after Apply. Buttons are disabled while the Replay Buffer
is not running.

## Versioning

This fork follows the upstream OBS version it is based on and adds its own
suffix. For example:

```text
33.0.0-rc2-multi.4
```

means "fork build 4 based on upstream OBS 33.0.0-rc2". It is **not** an
official OBS 33.1.0 release. The Windows file version shows the numeric base
(`33.0.0`), while the release/tag and About window show the full fork version.

## Windows installation

The GitHub Actions workflow produces a **ZIP archive, not a setup `.exe`**. It
is the same kind of ZIP that the normal OBS Windows build produces.

1. Download the latest `obs-studio-*-windows-x64.zip` (or
   `*-windows-arm64.zip` on Windows on ARM) from the repository's **Releases**
   page.
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

### Linux and macOS builds

The GitHub Actions workflow also produces:

- `obs-studio-*-x86_64-ubuntu-gnu.tar.xz` for Ubuntu/Linux x86_64
- `obs-studio-*-macos-apple.tar.xz` for Apple Silicon
- `obs-studio-*-macos-intel.tar.xz` for Intel Macs

The Linux archive contains the normal OBS install layout (`bin`, `lib`,
`share`). Run `bin/obs` from the extracted directory; if your desktop does not
find the bundled libraries, start it with
`LD_LIBRARY_PATH="$PWD/lib" ./bin/obs`. The macOS archive contains `OBS.app`;
it is unsigned, so right-click it and choose **Open** the first time.

These non-Windows builds are produced by the same OBS CI scripts but have not
been exercised as thoroughly as the Windows x64 build.

## Building from source

Requirements for the Windows build:

- Windows 10/11 x64
- Visual Studio 2022 or newer with the C++ desktop workload
- CMake 3.28 or newer
- Git, with submodules checked out

The same CMake project also builds on Linux and macOS using OBS's normal
platform prerequisites; those builds were not exercised locally.

```powershell
git clone --recurse-submodules https://github.com/JDuesterhus/obs-studio.git
cd obs-studio

cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo --target obs-studio
```

The build downloads the OBS dependency bundle and Qt automatically. The
resulting application is in `build_x64/rundir/RelWithDebInfo/bin/64bit`.

### Automated tests

The keyframe-window logic and the disk writer/save path have dependency-free
unit tests:

```powershell
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 -DENABLE_REPLAY_BUFFER_TESTS=ON
cmake --build build_x64 --config RelWithDebInfo --target test_replay_buffer_window test_replay_buffer_disk test_replay_buffer_disk_nospace test_replay_buffer_disk_slow
ctest --test-dir build_x64 -C RelWithDebInfo -R test_replay_buffer --output-on-failure
```

`test_replay_buffer_disk` exercises the segment writer, partial/full clip
selection, retention pruning, multiple saves, keyframe-less failure, and
temporary-folder cleanup. Two small variants compile the same code with a
deliberately tiny free-space reserve and a tiny write queue to test the
"disk full" and "storage too slow" guards.

The GitHub Actions workflow runs these tests and builds the Windows, Linux, and
macOS packages.

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
| Temporary `.part` save then rename | Passed (RAM and disk; failed save left no clip or `.part`) |
| Replay Buffer dock hidden by default | Passed (not present until View > Docks) |
| Dock duration and full-buffer buttons | Passed (real clips saved) |
| German dock and settings terminology | Passed (Replaypuffer, no Wiedergabepuffer) |
| Disk-backed buffer, real OBS session | Passed |
| Disk mode: 10 s and 30 s hotkeys plus full save | Passed (keyframe-aligned clips) |
| Disk mode: multi-segment save | Passed (10 s request from a 45 s buffer) |
| Disk mode: retention after >2x buffer length | Passed (30 s window saved ~33 s) |
| Disk mode: bounded memory over 5 minutes | Passed (420.7 MB -> 422.0 MB working set) |
| Disk mode: 120 s request from a 5-minute buffer | Passed (122.4 s clip) |
| Disk mode: temporary files removed on stop | Passed |
| Disk mode: stale crash folder cleanup | Passed (segment/lock removed, unrelated file kept) |
| Disk mode: unwritable/invalid buffer folder | Passed (clear start error, no crash) |
| Disk mode: low free space at start | Passed (unit test with injected threshold) |
| Disk mode: write-queue overload guard | Passed (unit test) |
| Disk mode: MP4 + H.264 + AAC | Passed |
| Disk mode: MKV + H.264 + AAC | Passed |
| Disk mode: Explorer duration | Passed (00:00:33 for a 33.2 s clip) |
| RAM mode unchanged after disk changes | Passed (10 s and full saves) |
| CI: Windows x64 and ARM64 packages | Passed |
| CI: Linux x86_64 package | Passed |
| CI: macOS ARM64 and Intel packages | Passed |
| Unit tests for keyframe window and disk buffer | Passed |

Example measurements from the integration runs:

| Requested | Actual clip | Container |
| --- | --- | --- |
| Whole buffer | 27.9 s | MP4, Explorer: 00:00:27 |
| 5 s | 6.4 s | MP4, Explorer: 00:00:06 |
| 10 s | 10.1 s | MP4, Explorer: 00:00:10 |
| 20 s | 21.2 s | MP4, Explorer: 00:00:21 |
| 20 s from a 12 s buffer | 11.3 s | MP4 |
| Disk mode, whole 30 s window | 34.1 s | MP4 |
| Disk mode, 10 s request | 11.2 s | MP4 |
| Disk mode, 30 s request | 30.1 s | MP4 |
| Disk mode, partially filled 21 s buffer | 21.6 s | MP4, HEVC |
| Disk mode, 10 s request after 45 s | 13.3 s | MP4, multi-segment |

Known limitations and not tested yet:

- The runtime low-space path uses the same no-space error handling as the
  tested start-time guard, but a physical drive was not deliberately filled
  during a live session. The start-time low-space guard and the write-queue
  overload guard are covered by automated tests.
- A slow physical HDD, a network share, or a removable drive was not runtime
  tested. The writer queue stops the buffer instead of blocking or growing
  memory without bound, but sustained throughput depends on the drive.
- Other audio codecs (Opus, PCM, FLAC) and hardware encoders other than NVENC
  H.264/HEVC were not part of the recent replay tests.
- Pausing the buffer and multi-hour sessions were not exercised in a live run.
- Linux and macOS packages were built successfully in CI but were not run
  locally on those operating systems.
- Disk mode keeps as much footage as the configured duration needs; it does
  not impose a separate user-configurable size cap. Choose a duration and drive
  that fit your free space. The UI estimates the requirement and warns when
  space is tight.
- If OBS itself crashes while a save is being muxed, a temporary `.part` file
  can remain. It is not a normal clip and can be deleted manually. A normal
  stop joins the save thread before exiting, and a failed mux deletes its
  temporary file.
- Code signing / SmartScreen reputation for the released ZIP.
- The new storage strings are translated to German; other locales fall back to
  English until native speakers review them. Existing duration strings keep
  their earlier translations.

## Upstream maintenance

The feature is deliberately kept in a small number of places:

- `plugins/obs-ffmpeg/obs-ffmpeg-mux.c` and `replay-buffer-window.h`:
  duration-aware saving and keyframe selection.
- `plugins/obs-ffmpeg/replay-buffer-disk.*`: the self-contained disk writer,
  segmented packet log, retention, snapshot pinning, and cleanup.
- `frontend/utility/SimpleOutput.cpp`, `AdvancedOutput.cpp`:
  pass configured durations and the storage mode to the replay output.
- `frontend/widgets/OBSBasic_ReplayBuffer.cpp`, `OBSBasic_Hotkeys.cpp`:
  duration hotkeys and saving.
- `frontend/widgets/ReplayBufferDock.*`: the optional quick-save dock.
- `frontend/settings/ReplayBufferDurationsWidget.*`,
  `frontend/settings/ReplayBufferStorageWidget.*`,
  `frontend/utility/ReplayBufferDurations.*`:
  settings UI and shared formatting/parsing.
- `frontend/settings/OBSBasicSettings.*` and locale files:
  wiring the duration editor and storage selector into both output modes.
- `test/replay-buffer/`: unit tests for the packet-window logic and the disk
  writer/save/retention/no-space/queue-overload paths.
- `.github/workflows/fork-builds.yml`: Windows/Linux/macOS packages and tests.

## License

This fork is distributed under the same GNU General Public License v2 or later
as OBS Studio. See `COPYING`.
