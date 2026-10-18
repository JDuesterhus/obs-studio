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

#include <obs.h>

#include <stdbool.h>
#include <stdint.h>

struct replay_disk;
struct replay_disk_save;

enum replay_disk_error {
	REPLAY_DISK_OK = 0,
	REPLAY_DISK_ERROR_PATH,
	REPLAY_DISK_ERROR_IO,
	REPLAY_DISK_ERROR_NO_SPACE,
	REPLAY_DISK_ERROR_QUEUE,
	REPLAY_DISK_ERROR_NO_KEYFRAME,
};

/*
 * Creates a disk-backed rolling buffer in `directory`.  Encoded packets are
 * appended to segment files and the oldest segments are removed once they are
 * outside the retention window.  `max_time_usec` is the retention window.
 *
 * On failure, returns NULL and, when `error_detail` is not NULL, stores a
 * human-readable detail string allocated with bstrdup (release with bfree).
 */
struct replay_disk *replay_disk_create(const char *directory, int64_t max_time_usec, char **error_detail);

/* Stops the writer, removes this session's temporary files, and frees memory. */
void replay_disk_destroy(struct replay_disk *disk);

/* Capture-thread entry point.  The packet data is retained until written. */
bool replay_disk_write(struct replay_disk *disk, const struct encoder_packet *packet);

/*
 * Capture-thread entry point for a save.  Enqueues the packet and marks it as
 * the end of the snapshot.  The returned save object must be waited on and
 * read from the mux thread, then released with replay_disk_save_free().
 * Returns NULL if the packet could not be queued.
 */
struct replay_disk_save *replay_disk_write_and_save(struct replay_disk *disk, const struct encoder_packet *packet,
						    int64_t duration_usec);

/*
 * Waits until the snapshot boundary has been written to disk.  Returns false if
 * the buffer failed, the save was cancelled, or the writer stopped.
 */
bool replay_disk_save_wait(struct replay_disk_save *save);

/*
 * Streams the selected records in capture order.  The callback receives a
 * packet whose data pointer is valid only for the duration of the call.  The
 * callback must return false to abort (for example when muxing fails).
 */
bool replay_disk_save_read(struct replay_disk_save *save,
			   bool (*callback)(void *context, struct encoder_packet *packet), void *context);

/* Cancels a wait/read and wakes the writer.  Safe to call from another thread. */
void replay_disk_save_cancel(struct replay_disk_save *save);

/* True when the save was cancelled (for example during shutdown). */
bool replay_disk_save_cancelled(const struct replay_disk_save *save);

/* Releases the snapshot and unpins its segment files. */
void replay_disk_save_free(struct replay_disk_save *save);

/* Last error on the disk buffer; `detail` receives an internal detail string. */
enum replay_disk_error replay_disk_get_error(struct replay_disk *disk, const char **detail);

/* Last error on a save snapshot, for logging. */
enum replay_disk_error replay_disk_save_error(const struct replay_disk_save *save, const char **detail);

/* Duration of the snapshot in microseconds (available after a successful wait). */
int64_t replay_disk_save_duration(const struct replay_disk_save *save);

#ifdef REPLAY_DISK_TEST_HOOKS
/* Test-only override for the runtime free-space check. */
void replay_disk_test_set_free_space(struct replay_disk *disk, int64_t free_space);
#endif
