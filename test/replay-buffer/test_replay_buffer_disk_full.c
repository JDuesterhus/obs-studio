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

#define CHECK(condition)                                                                      \
	do {                                                                                  \
		if (!(condition)) {                                                           \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
				#condition);                                                  \
			exit(1);                                                              \
		}                                                                             \
	} while (0)

int main(void)
{
	char cwd[1024];
	struct dstr root = {0};
	char *error = NULL;
	struct replay_disk *disk;
	struct replay_disk_save *save;
	const char *detail = NULL;
	struct encoder_packet packet = {0};
	long *refs;

	CHECK(os_getcwd(cwd, sizeof(cwd)) != NULL);
	dstr_printf(&root, "%s/replay-disk-full-test-%llu", cwd, (unsigned long long)os_gettime_ns());
	CHECK(os_mkdirs(root.array) == 0);

	disk = replay_disk_create(root.array, 10 * 1000000LL, &error);
	CHECK(disk != NULL);

	replay_disk_test_set_free_space(disk, 0);

	refs = bmalloc(sizeof(long) + 128);
	*refs = 1;
	packet.type = OBS_ENCODER_VIDEO;
	packet.keyframe = true;
	packet.track_idx = 0;
	packet.dts = 0;
	packet.pts = 0;
	packet.dts_usec = 0;
	packet.sys_dts_usec = 0;
	packet.timebase_num = 1;
	packet.timebase_den = 90000;
	packet.size = 128;
	packet.data = (uint8_t *)(refs + 1);

	save = replay_disk_write_and_save(disk, &packet, 0);
	obs_encoder_packet_release(&packet);
	CHECK(save != NULL);
	CHECK(!replay_disk_save_wait(save));
	CHECK(replay_disk_save_error(save, &detail) == REPLAY_DISK_ERROR_NO_SPACE);
	CHECK(detail != NULL);

	replay_disk_save_free(save);
	replay_disk_destroy(disk);

	os_dir_t *dir = os_opendir(root.array);
	struct os_dirent *entry;
	size_t remaining = 0;

	CHECK(dir != NULL);
	while ((entry = os_readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.' &&
		    (entry->d_name[1] == 0 || (entry->d_name[1] == '.' && entry->d_name[2] == 0)))
			continue;
		fprintf(stderr, "leftover buffer file: %s\n", entry->d_name);
		remaining++;
	}
	os_closedir(dir);
	CHECK(remaining == 0);
	CHECK(os_rmdir(root.array) == 0);

	dstr_free(&root);
	return 0;
}
