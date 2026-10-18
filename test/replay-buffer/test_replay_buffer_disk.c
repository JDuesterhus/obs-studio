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

#include "replay-buffer-disk.h"

#include <util/dstr.h>
#include <util/platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                      \
	do {                                                                                  \
		if (!(condition)) {                                                           \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
				#condition);                                                  \
			exit(1);                                                              \
		}                                                                             \
	} while (0)

#define SECONDS(x) ((int64_t)(x) * 1000000LL)

struct collected {
	size_t count;
	size_t video;
	size_t audio;
	int64_t first_video;
	int64_t last_video;
	int64_t last_dts;
	int64_t previous_dts;
	bool first_video_keyframe;
	bool monotonic;
};

static struct encoder_packet make_packet(bool video, bool keyframe, int track, int64_t dts_usec, size_t size)
{
	struct encoder_packet packet = {0};
	long *refs = bmalloc(sizeof(long) + size);

	*refs = 1;
	memset(refs + 1, 0x5a, size);

	packet.type = video ? OBS_ENCODER_VIDEO : OBS_ENCODER_AUDIO;
	packet.keyframe = keyframe;
	packet.track_idx = track;
	packet.dts_usec = dts_usec;
	packet.sys_dts_usec = dts_usec;
	packet.dts = dts_usec * 9 / 100;
	packet.pts = packet.dts;
	packet.timebase_num = 1;
	packet.timebase_den = 90000;
	packet.size = size;
	packet.data = (uint8_t *)(refs + 1);
	return packet;
}

static void write_packet(struct replay_disk *disk, bool video, bool keyframe, int track, int64_t dts_usec)
{
	struct encoder_packet packet = make_packet(video, keyframe, track, dts_usec, 128);

	CHECK(replay_disk_write(disk, &packet));
	obs_encoder_packet_release(&packet);
}

static bool collect_packet(void *data, struct encoder_packet *packet)
{
	struct collected *collected = data;

	if (collected->count > 0 && packet->dts_usec < collected->previous_dts) {
		collected->monotonic = false;
	}

	collected->previous_dts = packet->dts_usec;
	collected->last_dts = packet->dts_usec;
	collected->count++;

	if (packet->type == OBS_ENCODER_VIDEO) {
		if (collected->video == 0) {
			collected->first_video = packet->dts_usec;
			collected->first_video_keyframe = packet->keyframe;
		}
		collected->last_video = packet->dts_usec;
		collected->video++;
	} else {
		collected->audio++;
	}

	return true;
}

static struct replay_disk *create_buffer(const char *root, int64_t max_time_usec)
{
	char *error = NULL;
	struct replay_disk *disk = replay_disk_create(root, max_time_usec, &error);

	if (!disk) {
		fprintf(stderr, "replay_disk_create failed: %s\n", error ? error : "unknown error");
		bfree(error);
		exit(1);
	}
	return disk;
}

static void check_cleanup(const char *root)
{
	os_dir_t *dir = os_opendir(root);
	struct os_dirent *entry;
	size_t remaining = 0;

	CHECK(dir != NULL);
	while ((entry = os_readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.' &&
		    (entry->d_name[1] == 0 || (entry->d_name[1] == '.' && entry->d_name[2] == 0))) {
			continue;
		}
		fprintf(stderr, "leftover buffer file: %s\n", entry->d_name);
		remaining++;
	}
	os_closedir(dir);
	CHECK(remaining == 0);
	CHECK(os_rmdir(root) == 0);
}

static const char *make_root(struct dstr *root)
{
	char cwd[1024];

	CHECK(os_getcwd(cwd, sizeof(cwd)) != NULL);
	dstr_printf(root, "%s/replay-disk-test-%llu", cwd, (unsigned long long)os_gettime_ns());
	CHECK(os_mkdirs(root->array) == 0);
	return root->array;
}

static void test_partial_and_full_window(void)
{
	struct dstr root = {0};
	const char *path = make_root(&root);
	struct replay_disk *disk = create_buffer(path, SECONDS(20));
	struct replay_disk_save *save;
	struct collected collected = {.monotonic = true};
	int64_t target = SECONDS(10);
	int64_t cutoff = target - SECONDS(5);

	for (int step = 0; step < 100; step++) {
		int64_t dts = (int64_t)step * 100000;
		write_packet(disk, true, step % 20 == 0, 0, dts);
		write_packet(disk, false, false, 0, dts);
	}

	struct encoder_packet packet = make_packet(true, true, 0, target, 128);
	save = replay_disk_write_and_save(disk, &packet, SECONDS(5));
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(replay_disk_save_wait(save));
	CHECK(replay_disk_save_read(save, collect_packet, &collected));

	CHECK(collected.count > 0);
	CHECK(collected.video > 0);
	CHECK(collected.audio > 0);
	CHECK(collected.monotonic);
	CHECK(collected.first_video_keyframe);
	CHECK(collected.first_video <= cutoff);
	CHECK(collected.first_video >= cutoff - SECONDS(2));
	CHECK(collected.last_video == target);
	CHECK(replay_disk_save_duration(save) > SECONDS(5));
	CHECK(replay_disk_save_duration(save) <= SECONDS(7));

	replay_disk_save_free(save);
	replay_disk_destroy(disk);
	check_cleanup(path);
	dstr_free(&root);
}

static void test_retention_window(void)
{
	struct dstr root = {0};
	const char *path = make_root(&root);
	struct replay_disk *disk = create_buffer(path, SECONDS(2));
	struct replay_disk_save *save;
	struct collected collected = {.monotonic = true};
	int64_t target = SECONDS(20);

	for (int step = 0; step < 200; step++) {
		int64_t dts = (int64_t)step * 100000;
		write_packet(disk, true, step % 20 == 0, 0, dts);
		write_packet(disk, false, false, 0, dts);
	}

	struct encoder_packet packet = make_packet(true, true, 0, target, 128);
	save = replay_disk_write_and_save(disk, &packet, 0);
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(replay_disk_save_wait(save));
	CHECK(replay_disk_save_read(save, collect_packet, &collected));

	/* Retention is 2 seconds plus one segment; the save must not contain
	 * the whole 20 second run. */
	CHECK(replay_disk_save_duration(save) >= SECONDS(2));
	CHECK(replay_disk_save_duration(save) <= SECONDS(8));
	CHECK(collected.last_video == target);

	replay_disk_save_free(save);
	replay_disk_destroy(disk);
	check_cleanup(path);
	dstr_free(&root);
}

static void test_no_keyframe(void)
{
	struct dstr root = {0};
	const char *path = make_root(&root);
	struct replay_disk *disk = create_buffer(path, SECONDS(10));
	struct replay_disk_save *save;
	const char *detail = NULL;

	for (int step = 0; step < 10; step++) {
		write_packet(disk, false, false, 0, (int64_t)step * 100000);
	}

	struct encoder_packet packet = make_packet(false, false, 0, SECONDS(1), 128);
	save = replay_disk_write_and_save(disk, &packet, 0);
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(!replay_disk_save_wait(save));
	CHECK(replay_disk_save_error(save, &detail) == REPLAY_DISK_ERROR_NO_KEYFRAME);
	CHECK(detail != NULL);

	replay_disk_save_free(save);
	replay_disk_destroy(disk);
	check_cleanup(path);
	dstr_free(&root);
}

static void test_multiple_saves(void)
{
	struct dstr root = {0};
	const char *path = make_root(&root);
	struct replay_disk *disk = create_buffer(path, SECONDS(20));
	struct collected first = {.monotonic = true};
	struct collected second = {.monotonic = true};
	struct replay_disk_save *save;
	int64_t target = SECONDS(6);

	for (int step = 0; step < 60; step++) {
		int64_t dts = (int64_t)step * 100000;
		write_packet(disk, true, step % 20 == 0, 0, dts);
		write_packet(disk, false, false, 0, dts);
	}

	struct encoder_packet packet = make_packet(true, true, 0, target, 128);
	save = replay_disk_write_and_save(disk, &packet, SECONDS(3));
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(replay_disk_save_wait(save));
	CHECK(replay_disk_save_read(save, collect_packet, &first));
	CHECK(first.count > 0);
	CHECK(first.last_video == target);
	replay_disk_save_free(save);

	for (int step = 60; step < 120; step++) {
		int64_t dts = (int64_t)step * 100000;
		write_packet(disk, true, step % 20 == 0, 0, dts);
		write_packet(disk, false, false, 0, dts);
	}

	target = SECONDS(12);
	packet = make_packet(true, true, 0, target, 128);
	save = replay_disk_write_and_save(disk, &packet, SECONDS(3));
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(replay_disk_save_wait(save));
	CHECK(replay_disk_save_read(save, collect_packet, &second));
	CHECK(second.count > 0);
	CHECK(second.last_video == target);
	CHECK(second.first_video > first.last_video);
	replay_disk_save_free(save);

	replay_disk_destroy(disk);
	check_cleanup(path);
	dstr_free(&root);
}

int main(void)
{
	test_partial_and_full_window();
	test_retention_window();
	test_no_keyframe();
	test_multiple_saves();
	return 0;
}
