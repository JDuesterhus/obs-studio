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

int main(void)
{
	char cwd[1024];
	struct dstr root = {0};
	char *error = NULL;
	struct replay_disk *disk;

	CHECK(os_getcwd(cwd, sizeof(cwd)) != NULL);
	dstr_printf(&root, "%s/replay-disk-nospace-test-%llu", cwd, (unsigned long long)os_gettime_ns());
	CHECK(os_mkdirs(root.array) == 0);

	disk = replay_disk_create(root.array, 10 * 1000000LL, &error);
	CHECK(disk == NULL);
	CHECK(error != NULL);
	CHECK(strstr(error, "not enough free disk space") != NULL);

	bfree(error);

	os_dir_t *dir = os_opendir(root.array);
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
	CHECK(os_rmdir(root.array) == 0);

	dstr_free(&root);
	return 0;
}
