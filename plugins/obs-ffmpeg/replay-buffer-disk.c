/******************************************************************************
    Copyright (C) 2026 by Jeremy DÃ¼sterhus

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

#include "replay-buffer-disk.h"

#include <util/darray.h>
#include <util/deque.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define REPLAY_DISK_RECORD_MAGIC 0x4f425344u /* "OBSD" */
#define REPLAY_DISK_RECORD_VERSION 1
#define REPLAY_DISK_SEGMENT_BYTES (128 * 1024 * 1024)
#ifndef REPLAY_DISK_QUEUE_LIMIT
#define REPLAY_DISK_QUEUE_LIMIT (64 * 1024 * 1024)
#endif
#ifndef REPLAY_DISK_MIN_FREE_SPACE
#define REPLAY_DISK_MIN_FREE_SPACE (512 * 1024 * 1024)
#endif
#ifndef REPLAY_DISK_SPACE_CHECK_BYTES
#define REPLAY_DISK_SPACE_CHECK_BYTES (32 * 1024 * 1024)
#endif
#define REPLAY_DISK_MAX_PACKET_BYTES (64 * 1024 * 1024)
#define REPLAY_DISK_STALE_SECONDS (24 * 60 * 60)

struct replay_disk_record {
	uint32_t magic;
	uint16_t version;
	uint8_t type;
	uint8_t keyframe;
	uint32_t track_idx;
	uint32_t size;
	int64_t pts;
	int64_t dts;
	int64_t dts_usec;
	int64_t sys_dts_usec;
	int32_t timebase_num;
	int32_t timebase_den;
};

struct replay_disk_segment {
	uint32_t index;
	struct dstr path;
	FILE *file;
	int64_t size;
	int64_t start_dts_usec;
	int64_t end_dts_usec;
	int refs;
	bool expired;
};

struct replay_disk_keyframe {
	uint64_t seq;
	int64_t dts_usec;
	struct replay_disk_segment *segment;
	int64_t offset;
};

struct replay_disk_item {
	struct encoder_packet packet;
	uint64_t seq;
};

struct replay_disk_save {
	struct replay_disk *disk;
	uint64_t target_seq;
	int64_t target_dts_usec;
	int64_t requested_duration_usec;
	bool ready;
	bool failed;
	bool cancelled;
	enum replay_disk_error error;
	char detail[256];
	DARRAY(struct replay_disk_segment *) segments;
	int64_t start_offset;
	int64_t end_offset;
	int64_t start_dts_usec;
	int64_t end_dts_usec;
};

struct replay_disk {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t writer;
	bool writer_started;
	bool stopping;

	enum replay_disk_error error;
	char error_detail[256];

	struct dstr directory;
	struct dstr session_dir;
	struct dstr lock_path;
	FILE *lock_file;
	time_t last_lock_update;

	struct deque queue;
	size_t queue_bytes;
	uint64_t next_seq;
	uint64_t written_seq;

	DARRAY(struct replay_disk_segment *) segments;
	DARRAY(struct replay_disk_keyframe) keyframes;
	struct replay_disk_segment *current;
	uint32_t next_segment_index;

	int64_t max_time_usec;
	int64_t segment_duration_usec;
	int64_t total_bytes;
	int64_t newest_dts_usec;
	int64_t bytes_since_space_check;
#ifdef REPLAY_DISK_TEST_HOOKS
	int64_t test_free_space;
#endif
	struct replay_disk_save *pending_save;
};

static void set_error_locked(struct replay_disk *disk, enum replay_disk_error error, const char *format, ...)
{
	va_list args;

	if (disk->error != REPLAY_DISK_OK) {
		return;
	}

	disk->error = error;
	va_start(args, format);
	vsnprintf(disk->error_detail, sizeof(disk->error_detail), format, args);
	va_end(args);

	if (disk->pending_save) {
		struct replay_disk_save *save = disk->pending_save;
		save->failed = true;
		save->error = error;
		snprintf(save->detail, sizeof(save->detail), "%s", disk->error_detail);
		disk->pending_save = NULL;
	}

	pthread_cond_broadcast(&disk->cond);
}

static void set_error(struct replay_disk *disk, enum replay_disk_error error, const char *format, ...)
{
	va_list args;

	pthread_mutex_lock(&disk->mutex);
	if (disk->error == REPLAY_DISK_OK) {
		disk->error = error;
		va_start(args, format);
		vsnprintf(disk->error_detail, sizeof(disk->error_detail), format, args);
		va_end(args);
	}
	pthread_mutex_unlock(&disk->mutex);
}

static void refresh_lock_file(struct replay_disk *disk)
{
	time_t now;

	if (!disk->lock_file) {
		return;
	}

	now = time(NULL);
	if (now - disk->last_lock_update < 60) {
		return;
	}

	fputc('.', disk->lock_file);
	fflush(disk->lock_file);
	disk->last_lock_update = now;
}

static void fail_save_locked(struct replay_disk_save *save, enum replay_disk_error error, const char *format, ...)
{
	va_list args;

	if (!save || save->ready || save->failed) {
		return;
	}

	save->failed = true;
	save->error = error;
	va_start(args, format);
	vsnprintf(save->detail, sizeof(save->detail), format, args);
	va_end(args);

	if (save->disk->pending_save == save) {
		save->disk->pending_save = NULL;
	}

	pthread_cond_broadcast(&save->disk->cond);
}

static void fail_save(struct replay_disk_save *save, enum replay_disk_error error, const char *format, ...)
{
	va_list args;

	if (!save) {
		return;
	}

	pthread_mutex_lock(&save->disk->mutex);
	if (!save->failed) {
		save->failed = true;
		save->error = error;
		va_start(args, format);
		vsnprintf(save->detail, sizeof(save->detail), format, args);
		va_end(args);
		if (save->disk->pending_save == save) {
			save->disk->pending_save = NULL;
		}
	}
	pthread_cond_broadcast(&save->disk->cond);
	pthread_mutex_unlock(&save->disk->mutex);
}

static void destroy_segment_locked(struct replay_disk *disk, struct replay_disk_segment *segment)
{
	if (!segment) {
		return;
	}

	if (segment->file) {
		fclose(segment->file);
		segment->file = NULL;
	}
	if (segment->path.array && *segment->path.array) {
		os_unlink(segment->path.array);
	}

	disk->total_bytes -= segment->size;
	dstr_free(&segment->path);
	bfree(segment);
}

static void delete_expired_segments_locked(struct replay_disk *disk)
{
	for (size_t i = 0; i < disk->segments.num;) {
		struct replay_disk_segment *segment = disk->segments.array[i];

		if (!segment->expired || segment->refs > 0) {
			i++;
			continue;
		}

		da_erase(disk->segments, i);
		destroy_segment_locked(disk, segment);
	}
}

static void remove_keyframes_for_segment_locked(struct replay_disk *disk, struct replay_disk_segment *segment)
{
	for (size_t i = 0; i < disk->keyframes.num;) {
		if (disk->keyframes.array[i].segment == segment) {
			da_erase(disk->keyframes, i);
		} else {
			i++;
		}
	}
}

static void prune_segments_locked(struct replay_disk *disk)
{
	while (disk->segments.num > 2) {
		struct replay_disk_segment *segment = disk->segments.array[0];

		if (!segment->expired) {
			/*
			 * Retention is measured from the segment end.  The remaining
			 * oldest segment may start slightly before the window, which
			 * keeps the keyframe at or before the requested start available.
			 * This bounds the full buffer to roughly the configured
			 * duration plus one segment.
			 */
			if (disk->max_time_usec <= 0 ||
			    disk->newest_dts_usec - segment->end_dts_usec <= disk->max_time_usec) {
				break;
			}

			segment->expired = true;
			remove_keyframes_for_segment_locked(disk, segment);
		}

		if (segment->refs > 0) {
			break;
		}

		da_erase(disk->segments, 0);
		destroy_segment_locked(disk, segment);
	}
}

static bool create_segment_locked(struct replay_disk *disk)
{
	struct replay_disk_segment *segment = bzalloc(sizeof(*segment));

	segment->index = disk->next_segment_index++;
	dstr_printf(&segment->path, "%s/segment-%06u.obsrb", disk->session_dir.array, segment->index);

	segment->file = os_fopen(segment->path.array, "wb");
	if (!segment->file) {
		set_error_locked(disk, REPLAY_DISK_ERROR_IO, "could not create segment '%s': %s", segment->path.array,
				 strerror(errno));
		dstr_free(&segment->path);
		bfree(segment);
		return false;
	}

	da_push_back(disk->segments, &segment);
	disk->current = segment;
	return true;
}

static bool roll_segment_locked(struct replay_disk *disk)
{
	if (disk->current && disk->current->file) {
		bool flush_failed = fflush(disk->current->file) != 0;
		bool close_failed = fclose(disk->current->file) != 0;
		disk->current->file = NULL;

		if (flush_failed || close_failed) {
			set_error_locked(disk, REPLAY_DISK_ERROR_IO, "could not finish replay buffer segment: %s",
					 strerror(errno));
			return false;
		}
	}

	return create_segment_locked(disk);
}

static bool capture_save_locked(struct replay_disk *disk, struct replay_disk_save *save, int64_t target_dts_usec)
{
	int64_t cutoff = INT64_MIN;
	int first_keyframe = -1;
	int best_keyframe = -1;
	int start_segment = -1;
	int end_segment = -1;
	struct replay_disk_keyframe *keyframe;

	if (!save) {
		return false;
	}
	if (save->target_seq == 0 || disk->segments.num == 0) {
		fail_save_locked(save, REPLAY_DISK_ERROR_NO_KEYFRAME, "the replay buffer is empty");
		return false;
	}

	if (save->requested_duration_usec > 0) {
		cutoff = target_dts_usec > INT64_MIN + save->requested_duration_usec
				 ? target_dts_usec - save->requested_duration_usec
				 : INT64_MIN;
	}

	for (size_t i = 0; i < disk->keyframes.num; i++) {
		struct replay_disk_keyframe *candidate = &disk->keyframes.array[i];

		if (candidate->seq > save->target_seq) {
			break;
		}
		if (first_keyframe < 0) {
			first_keyframe = (int)i;
		}
		if (candidate->dts_usec <= cutoff) {
			best_keyframe = (int)i;
		}
	}

	if (save->requested_duration_usec <= 0) {
		best_keyframe = first_keyframe;
	} else if (best_keyframe < 0) {
		best_keyframe = first_keyframe;
	}

	if (best_keyframe < 0) {
		fail_save_locked(save, REPLAY_DISK_ERROR_NO_KEYFRAME, "no video keyframe is available");
		return false;
	}

	keyframe = &disk->keyframes.array[best_keyframe];

	for (size_t i = 0; i < disk->segments.num; i++) {
		if (disk->segments.array[i] == keyframe->segment) {
			start_segment = (int)i;
		}
		if (disk->segments.array[i] == disk->current) {
			end_segment = (int)i;
		}
	}

	if (start_segment < 0 || end_segment < start_segment) {
		fail_save_locked(save, REPLAY_DISK_ERROR_IO, "could not locate the keyframe segment");
		return false;
	}

	save->start_offset = keyframe->offset;
	save->end_offset = disk->current->size;
	save->start_dts_usec = keyframe->dts_usec;
	save->end_dts_usec = target_dts_usec;

	/*
	 * Offsets are relative to their own segment, so they can only be compared
	 * when the snapshot starts and ends in the same segment.
	 */
	if ((start_segment == end_segment && save->start_offset >= save->end_offset) ||
	    save->start_offset >= keyframe->segment->size || save->end_offset <= 0) {
		fail_save_locked(save, REPLAY_DISK_ERROR_NO_KEYFRAME, "the selected keyframe has no following packets");
		return false;
	}

	for (int i = start_segment; i <= end_segment; i++) {
		struct replay_disk_segment *segment = disk->segments.array[i];
		segment->refs++;
		da_push_back(save->segments, &segment);
	}

	save->ready = true;
	disk->pending_save = NULL;
	pthread_cond_broadcast(&disk->cond);
	return true;
}

static bool write_record(struct replay_disk *disk, struct encoder_packet *packet, uint64_t seq)
{
	struct replay_disk_record record = {0};
	struct replay_disk_segment *segment;
	int64_t offset;
	size_t record_size;
	bool keyframe = packet->type == OBS_ENCODER_VIDEO && packet->keyframe;
	bool ok;

	if (packet->size > REPLAY_DISK_MAX_PACKET_BYTES || packet->size > UINT32_MAX) {
		pthread_mutex_lock(&disk->mutex);
		set_error_locked(disk, REPLAY_DISK_ERROR_IO, "encoded packet is too large (%zu bytes)", packet->size);
		pthread_mutex_unlock(&disk->mutex);
		return false;
	}

	record_size = sizeof(record) + packet->size;

	pthread_mutex_lock(&disk->mutex);
	if (!disk->current && !create_segment_locked(disk)) {
		pthread_mutex_unlock(&disk->mutex);
		return false;
	}

	segment = disk->current;
	bool time_to_roll = segment->size > 0 &&
			    packet->dts_usec - segment->start_dts_usec >= disk->segment_duration_usec;
	if (time_to_roll || (segment->size > 0 && segment->size + (int64_t)record_size > REPLAY_DISK_SEGMENT_BYTES)) {
		if (!roll_segment_locked(disk)) {
			pthread_mutex_unlock(&disk->mutex);
			return false;
		}
		segment = disk->current;
	}
	offset = segment->size;
	pthread_mutex_unlock(&disk->mutex);

	record.magic = REPLAY_DISK_RECORD_MAGIC;
	record.version = REPLAY_DISK_RECORD_VERSION;
	record.type = (uint8_t)packet->type;
	record.keyframe = keyframe ? 1 : 0;
	record.track_idx = (uint32_t)packet->track_idx;
	record.size = (uint32_t)packet->size;
	record.pts = packet->pts;
	record.dts = packet->dts;
	record.dts_usec = packet->dts_usec;
	record.sys_dts_usec = packet->sys_dts_usec;
	record.timebase_num = packet->timebase_num;
	record.timebase_den = packet->timebase_den;

	ok = fwrite(&record, 1, sizeof(record), segment->file) == sizeof(record);
	if (ok && packet->size) {
		ok = fwrite(packet->data, 1, packet->size, segment->file) == packet->size;
	}

	if (!ok) {
		int write_errno = errno;

		pthread_mutex_lock(&disk->mutex);
		set_error_locked(disk, write_errno == ENOSPC ? REPLAY_DISK_ERROR_NO_SPACE : REPLAY_DISK_ERROR_IO,
				 "could not write replay buffer segment: %s", strerror(write_errno));
		pthread_mutex_unlock(&disk->mutex);
		return false;
	}

	pthread_mutex_lock(&disk->mutex);
	if (segment->size == 0) {
		segment->start_dts_usec = packet->dts_usec;
	}
	segment->size += (int64_t)record_size;
	segment->end_dts_usec = packet->dts_usec;

	disk->total_bytes += (int64_t)record_size;
	disk->newest_dts_usec = packet->dts_usec;
	disk->written_seq = seq;
	disk->bytes_since_space_check += (int64_t)record_size;

	if (keyframe) {
		struct replay_disk_keyframe keyframe_entry = {
			.seq = seq,
			.dts_usec = packet->dts_usec,
			.segment = segment,
			.offset = offset,
		};
		da_push_back(disk->keyframes, &keyframe_entry);
	}

	prune_segments_locked(disk);

	if (disk->bytes_since_space_check >= REPLAY_DISK_SPACE_CHECK_BYTES) {
		int64_t free_space = os_get_free_space(disk->session_dir.array);
#ifdef REPLAY_DISK_TEST_HOOKS
		if (disk->test_free_space >= 0)
			free_space = disk->test_free_space;
#endif
		disk->bytes_since_space_check = 0;

		if (free_space >= 0 && free_space < REPLAY_DISK_MIN_FREE_SPACE) {
			set_error_locked(disk, REPLAY_DISK_ERROR_NO_SPACE,
					 "the disk is low on free space (%lld MB remaining)",
					 (long long)(free_space / (1024 * 1024)));
		}
	}

	bool capture = disk->pending_save && seq >= disk->pending_save->target_seq;
	pthread_mutex_unlock(&disk->mutex);

	if (capture) {
		if (fflush(segment->file) != 0) {
			pthread_mutex_lock(&disk->mutex);
			set_error_locked(disk, REPLAY_DISK_ERROR_IO, "could not flush replay buffer segment: %s",
					 strerror(errno));
			pthread_mutex_unlock(&disk->mutex);
			return false;
		}

		pthread_mutex_lock(&disk->mutex);
		if (disk->pending_save && seq >= disk->pending_save->target_seq) {
			struct replay_disk_save *save = disk->pending_save;
			if (!capture_save_locked(disk, save, packet->dts_usec)) {
				disk->pending_save = NULL;
			}
		}
		pthread_mutex_unlock(&disk->mutex);
	}

	return true;
}

static void *writer_thread(void *data)
{
	struct replay_disk *disk = data;

	for (;;) {
		struct replay_disk_item item;

		pthread_mutex_lock(&disk->mutex);
		while (!disk->stopping && disk->error == REPLAY_DISK_OK && disk->queue.size == 0) {
			pthread_cond_wait(&disk->cond, &disk->mutex);
		}

		if (disk->stopping || disk->error != REPLAY_DISK_OK) {
			if (disk->pending_save) {
				struct replay_disk_save *save = disk->pending_save;
				if (disk->stopping) {
					save->cancelled = true;
				} else {
					fail_save_locked(save, disk->error, "%s", disk->error_detail);
				}
				disk->pending_save = NULL;
			}
			pthread_cond_broadcast(&disk->cond);
			pthread_mutex_unlock(&disk->mutex);
			break;
		}

		deque_pop_front(&disk->queue, &item, sizeof(item));
		disk->queue_bytes -= item.packet.size;
		pthread_mutex_unlock(&disk->mutex);

		bool ok = write_record(disk, &item.packet, item.seq);
		obs_encoder_packet_release(&item.packet);
		refresh_lock_file(disk);

		if (!ok) {
			pthread_mutex_lock(&disk->mutex);
			if (disk->pending_save) {
				fail_save_locked(disk->pending_save, disk->error, "%s", disk->error_detail);
			}
			disk->pending_save = NULL;
			pthread_cond_broadcast(&disk->cond);
			pthread_mutex_unlock(&disk->mutex);
			break;
		}
	}

	return NULL;
}

static void destroy_session_dir(const char *path)
{
	os_dir_t *dir = os_opendir(path);

	if (dir) {
		struct os_dirent *entry;

		while ((entry = os_readdir(dir)) != NULL) {
			if (entry->directory) {
				continue;
			}

			if (strncmp(entry->d_name, "segment-", 8) == 0 || strcmp(entry->d_name, "session.lock") == 0) {
				struct dstr file_path = {0};
				dstr_printf(&file_path, "%s/%s", path, entry->d_name);
				os_unlink(file_path.array);
				dstr_free(&file_path);
			}
		}
		os_closedir(dir);
	}

	os_rmdir(path);
}

static void cleanup_stale_sessions(const char *directory)
{
	os_dir_t *dir = os_opendir(directory);
	time_t now = time(NULL);

	if (!dir) {
		return;
	}

	struct os_dirent *entry;
	while ((entry = os_readdir(dir)) != NULL) {
		struct dstr session_path = {0};
		struct dstr lock_path = {0};
		struct stat st;

		if (!entry->directory || strncmp(entry->d_name, "obs-replay-buffer-", 18) != 0) {
			continue;
		}

		dstr_printf(&session_path, "%s/%s", directory, entry->d_name);
		dstr_printf(&lock_path, "%s/session.lock", session_path.array);

		if (os_stat(lock_path.array, &st) == 0 && now - st.st_mtime > REPLAY_DISK_STALE_SECONDS) {
			destroy_session_dir(session_path.array);
		}

		dstr_free(&lock_path);
		dstr_free(&session_path);
	}

	os_closedir(dir);
}

static void free_queue_locked(struct replay_disk *disk)
{
	while (disk->queue.size > 0) {
		struct replay_disk_item item;
		deque_pop_front(&disk->queue, &item, sizeof(item));
		obs_encoder_packet_release(&item.packet);
	}
	disk->queue_bytes = 0;
}

void replay_disk_destroy(struct replay_disk *disk)
{
	if (!disk) {
		return;
	}

	pthread_mutex_lock(&disk->mutex);
	disk->stopping = true;
	pthread_cond_broadcast(&disk->cond);
	pthread_mutex_unlock(&disk->mutex);

	if (disk->writer_started) {
		pthread_join(disk->writer, NULL);
		disk->writer_started = false;
	}

	pthread_mutex_lock(&disk->mutex);
	free_queue_locked(disk);
	for (size_t i = 0; i < disk->segments.num; i++) {
		destroy_segment_locked(disk, disk->segments.array[i]);
	}
	da_free(disk->segments);
	da_free(disk->keyframes);
	if (disk->lock_file) {
		fclose(disk->lock_file);
		disk->lock_file = NULL;
	}
	pthread_mutex_unlock(&disk->mutex);

	if (disk->lock_path.array) {
		os_unlink(disk->lock_path.array);
	}
	if (disk->session_dir.array) {
		destroy_session_dir(disk->session_dir.array);
	}

	pthread_cond_destroy(&disk->cond);
	pthread_mutex_destroy(&disk->mutex);

	dstr_free(&disk->directory);
	dstr_free(&disk->session_dir);
	dstr_free(&disk->lock_path);
	bfree(disk);
}

struct replay_disk *replay_disk_create(const char *directory, int64_t max_time_usec, char **error_detail)
{
	struct replay_disk *disk;
	struct dstr root = {0};
	bool created = false;

	if (error_detail) {
		*error_detail = NULL;
	}

	if (!directory || !*directory) {
		if (error_detail) {
			*error_detail = bstrdup("the selected buffer folder is empty");
		}
		return NULL;
	}

	disk = bzalloc(sizeof(*disk));
	pthread_mutex_init(&disk->mutex, NULL);
	pthread_cond_init(&disk->cond, NULL);
	disk->max_time_usec = max_time_usec;
	disk->next_seq = 0;
#ifdef REPLAY_DISK_TEST_HOOKS
	disk->test_free_space = -1;
#endif

	/*
	 * Keep individual segments short enough that pruning can follow the
	 * configured retention window closely.  Without a time-based roll, a
	 * large byte-sized segment could retain far more footage than requested
	 * at low bitrates.
	 */
	if (max_time_usec > 0) {
		disk->segment_duration_usec = max_time_usec / 4;
		if (disk->segment_duration_usec < 2 * 1000000LL) {
			disk->segment_duration_usec = 2 * 1000000LL;
		}
		if (disk->segment_duration_usec > 60 * 1000000LL) {
			disk->segment_duration_usec = 60 * 1000000LL;
		}
	} else {
		disk->segment_duration_usec = 60 * 1000000LL;
	}

	dstr_copy(&root, directory);
	dstr_replace(&root, "\\", "/");
	while (dstr_end(&root) == '/' && root.len > 1) {
		dstr_resize(&root, root.len - 1);
	}
	dstr_copy(&disk->directory, root.array);

	cleanup_stale_sessions(disk->directory.array);

	for (int attempt = 0; attempt < 10 && !created; attempt++) {
		uint64_t stamp = (uint64_t)os_gettime_ns() + (uint64_t)attempt;
		dstr_printf(&disk->session_dir, "%s/obs-replay-buffer-%llu", disk->directory.array,
			    (unsigned long long)stamp);

		int ret = os_mkdirs(disk->session_dir.array);
		if (ret == 0) {
			created = true;
			break;
		}
		if (ret != MKDIR_EXISTS) {
			set_error(disk, REPLAY_DISK_ERROR_PATH, "could not create buffer folder '%s'",
				  disk->session_dir.array);
			break;
		}
	}

	if (!created) {
		if (disk->error == REPLAY_DISK_OK) {
			set_error(disk, REPLAY_DISK_ERROR_PATH, "could not create a unique buffer folder");
		}
		if (error_detail) {
			*error_detail = bstrdup(disk->error_detail);
		}
		replay_disk_destroy(disk);
		dstr_free(&root);
		return NULL;
	}

	dstr_printf(&disk->lock_path, "%s/session.lock", disk->session_dir.array);
	disk->lock_file = os_fopen(disk->lock_path.array, "wb");
	if (disk->lock_file) {
		fprintf(disk->lock_file, "OBS replay buffer session\n");
		fflush(disk->lock_file);
		disk->last_lock_update = time(NULL);
	} else {
		set_error(disk, REPLAY_DISK_ERROR_PATH, "could not create '%s': %s", disk->lock_path.array,
			  strerror(errno));
	}

	if (disk->error == REPLAY_DISK_OK) {
		int64_t free_space = os_get_free_space(disk->session_dir.array);

		if (free_space >= 0 && free_space < REPLAY_DISK_MIN_FREE_SPACE) {
			set_error(disk, REPLAY_DISK_ERROR_NO_SPACE,
				  "not enough free disk space (%lld MB available, %lld MB required)",
				  (long long)(free_space / (1024 * 1024)),
				  (long long)(REPLAY_DISK_MIN_FREE_SPACE / (1024 * 1024)));
		}
	}

	if (disk->error == REPLAY_DISK_OK) {
		pthread_mutex_lock(&disk->mutex);
		created = create_segment_locked(disk);
		pthread_mutex_unlock(&disk->mutex);
		if (!created) {
			disk->error = REPLAY_DISK_ERROR_IO;
		}
	}

	if (disk->error == REPLAY_DISK_OK) {
		disk->writer_started = pthread_create(&disk->writer, NULL, writer_thread, disk) == 0;
		if (!disk->writer_started) {
			set_error(disk, REPLAY_DISK_ERROR_IO, "could not start the disk writer thread");
		}
	}

	if (disk->error != REPLAY_DISK_OK) {
		if (error_detail) {
			*error_detail = bstrdup(disk->error_detail);
		}
		replay_disk_destroy(disk);
		dstr_free(&root);
		return NULL;
	}

	blog(LOG_INFO, "[replay buffer disk] using '%s'", disk->session_dir.array);
	dstr_free(&root);
	return disk;
}

bool replay_disk_write(struct replay_disk *disk, const struct encoder_packet *packet)
{
	struct replay_disk_item item = {0};
	bool ok = true;

	if (!disk || !packet) {
		return false;
	}

	pthread_mutex_lock(&disk->mutex);
	if (disk->error != REPLAY_DISK_OK || disk->stopping) {
		ok = false;
		goto unlock;
	}

	if (disk->queue_bytes + packet->size > REPLAY_DISK_QUEUE_LIMIT) {
		set_error_locked(disk, REPLAY_DISK_ERROR_QUEUE,
				 "the disk writer cannot keep up; queued replay data exceeded %d MB",
				 REPLAY_DISK_QUEUE_LIMIT / (1024 * 1024));
		ok = false;
		goto unlock;
	}

	item.seq = ++disk->next_seq;
	obs_encoder_packet_ref(&item.packet, (struct encoder_packet *)packet);
	disk->queue_bytes += packet->size;
	deque_push_back(&disk->queue, &item, sizeof(item));
	pthread_cond_signal(&disk->cond);

unlock:
	pthread_mutex_unlock(&disk->mutex);
	return ok;
}

struct replay_disk_save *replay_disk_write_and_save(struct replay_disk *disk, const struct encoder_packet *packet,
						    int64_t duration_usec)
{
	struct replay_disk_item item = {0};
	struct replay_disk_save *save = NULL;

	if (!disk || !packet) {
		return NULL;
	}

	pthread_mutex_lock(&disk->mutex);
	if (disk->error != REPLAY_DISK_OK || disk->stopping || disk->pending_save) {
		goto unlock;
	}

	if (disk->queue_bytes + packet->size > REPLAY_DISK_QUEUE_LIMIT) {
		set_error_locked(disk, REPLAY_DISK_ERROR_QUEUE,
				 "the disk writer cannot keep up; queued replay data exceeded %d MB",
				 REPLAY_DISK_QUEUE_LIMIT / (1024 * 1024));
		goto unlock;
	}

	save = bzalloc(sizeof(*save));
	save->disk = disk;
	save->target_dts_usec = packet->dts_usec;
	save->requested_duration_usec = duration_usec;

	item.seq = ++disk->next_seq;
	obs_encoder_packet_ref(&item.packet, (struct encoder_packet *)packet);
	disk->queue_bytes += packet->size;
	deque_push_back(&disk->queue, &item, sizeof(item));

	save->target_seq = item.seq;
	disk->pending_save = save;
	pthread_cond_signal(&disk->cond);

unlock:
	pthread_mutex_unlock(&disk->mutex);
	return save;
}

bool replay_disk_save_wait(struct replay_disk_save *save)
{
	bool ok;

	if (!save) {
		return false;
	}

	pthread_mutex_lock(&save->disk->mutex);
	while (!save->ready && !save->failed && !save->cancelled) {
		pthread_cond_wait(&save->disk->cond, &save->disk->mutex);
	}

	ok = save->ready && !save->failed && !save->cancelled;
	pthread_mutex_unlock(&save->disk->mutex);
	return ok;
}

bool replay_disk_save_read(struct replay_disk_save *save,
			   bool (*callback)(void *context, struct encoder_packet *packet), void *context)
{
	uint8_t *buffer = NULL;
	size_t buffer_size = 0;
	size_t packets = 0;

	if (!save || !save->ready || save->failed || save->cancelled || !callback) {
		return false;
	}

	for (size_t i = 0; i < save->segments.num; i++) {
		struct replay_disk_segment *segment = save->segments.array[i];
		int64_t start = i == 0 ? save->start_offset : 0;
		int64_t end = i + 1 == save->segments.num ? save->end_offset : segment->size;
		FILE *file = os_fopen(segment->path.array, "rb");

		if (!file) {
			fail_save(save, REPLAY_DISK_ERROR_IO, "could not open segment '%s': %s", segment->path.array,
				  strerror(errno));
			goto fail;
		}

		if (os_fseeki64(file, start, SEEK_SET) != 0) {
			fail_save(save, REPLAY_DISK_ERROR_IO, "could not seek in segment '%s'", segment->path.array);
			fclose(file);
			goto fail;
		}

		int64_t position = start;
		while (position < end) {
			struct replay_disk_record record;

			if (save->cancelled) {
				fail_save(save, REPLAY_DISK_ERROR_IO, "replay save was cancelled");
				fclose(file);
				goto fail;
			}

			if (fread(&record, 1, sizeof(record), file) != sizeof(record)) {
				fail_save(save, REPLAY_DISK_ERROR_IO, "truncated segment '%s'", segment->path.array);
				fclose(file);
				goto fail;
			}

			if (record.magic != REPLAY_DISK_RECORD_MAGIC || record.version != REPLAY_DISK_RECORD_VERSION ||
			    record.type > OBS_ENCODER_VIDEO ||
			    (record.type == OBS_ENCODER_AUDIO && record.track_idx >= MAX_AUDIO_MIXES) ||
			    (record.type == OBS_ENCODER_VIDEO && record.timebase_den <= 0) ||
			    record.size > REPLAY_DISK_MAX_PACKET_BYTES ||
			    position + (int64_t)sizeof(record) + (int64_t)record.size > end) {
				fail_save(save, REPLAY_DISK_ERROR_IO, "invalid record in segment '%s'",
					  segment->path.array);
				fclose(file);
				goto fail;
			}

			if (record.size > buffer_size) {
				buffer = brealloc(buffer, record.size);
				buffer_size = record.size;
			}

			if (record.size && fread(buffer, 1, record.size, file) != record.size) {
				fail_save(save, REPLAY_DISK_ERROR_IO, "truncated packet in segment '%s'",
					  segment->path.array);
				fclose(file);
				goto fail;
			}

			struct encoder_packet packet = {0};
			packet.type = (enum obs_encoder_type)record.type;
			packet.keyframe = record.keyframe != 0;
			packet.track_idx = record.track_idx;
			packet.pts = record.pts;
			packet.dts = record.dts;
			packet.dts_usec = record.dts_usec;
			packet.sys_dts_usec = record.sys_dts_usec;
			packet.timebase_num = record.timebase_num;
			packet.timebase_den = record.timebase_den;
			packet.size = record.size;
			packet.data = buffer;

			if (!callback(context, &packet)) {
				fail_save(save, REPLAY_DISK_ERROR_IO, "could not mux replay packet");
				fclose(file);
				goto fail;
			}

			packets++;
			position += (int64_t)sizeof(record) + (int64_t)record.size;
		}

		fclose(file);
	}

	bfree(buffer);

	if (!packets) {
		fail_save(save, REPLAY_DISK_ERROR_NO_KEYFRAME, "the selected snapshot is empty");
		return false;
	}
	return true;

fail:
	bfree(buffer);
	return false;
}

void replay_disk_save_cancel(struct replay_disk_save *save)
{
	if (!save) {
		return;
	}

	pthread_mutex_lock(&save->disk->mutex);
	save->cancelled = true;
	if (save->disk->pending_save == save) {
		save->disk->pending_save = NULL;
	}
	pthread_cond_broadcast(&save->disk->cond);
	pthread_mutex_unlock(&save->disk->mutex);
}

bool replay_disk_save_cancelled(const struct replay_disk_save *save)
{
	return save && save->cancelled;
}

void replay_disk_save_free(struct replay_disk_save *save)
{
	struct replay_disk *disk;

	if (!save) {
		return;
	}

	disk = save->disk;

	pthread_mutex_lock(&disk->mutex);
	for (size_t i = 0; i < save->segments.num; i++) {
		struct replay_disk_segment *segment = save->segments.array[i];
		if (segment->refs > 0) {
			segment->refs--;
		}
	}
	delete_expired_segments_locked(disk);
	pthread_mutex_unlock(&disk->mutex);

	da_free(save->segments);
	bfree(save);
}

#ifdef REPLAY_DISK_TEST_HOOKS
void replay_disk_test_set_free_space(struct replay_disk *disk, int64_t free_space)
{
	pthread_mutex_lock(&disk->mutex);
	disk->test_free_space = free_space;
	pthread_mutex_unlock(&disk->mutex);
}
#endif

enum replay_disk_error replay_disk_get_error(struct replay_disk *disk, const char **detail)
{
	if (!disk) {
		if (detail) {
			*detail = "disk buffer is not available";
		}
		return REPLAY_DISK_ERROR_PATH;
	}

	if (detail) {
		*detail = disk->error_detail;
	}
	return disk->error;
}

enum replay_disk_error replay_disk_save_error(const struct replay_disk_save *save, const char **detail)
{
	if (!save) {
		if (detail) {
			*detail = "save is not available";
		}
		return REPLAY_DISK_ERROR_IO;
	}

	if (detail) {
		*detail = save->detail;
	}
	return save->error;
}

int64_t replay_disk_save_duration(const struct replay_disk_save *save)
{
	if (!save || save->end_dts_usec < save->start_dts_usec) {
		return 0;
	}
	return save->end_dts_usec - save->start_dts_usec;
}
