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

#include "replay-buffer-window.h"

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

struct test_packet {
	int64_t dts_usec;
	bool video;
	bool keyframe;
};

static void get_packet(void *context, size_t index, struct replay_buffer_window_packet *packet)
{
	const struct test_packet *packets = context;

	packet->dts_usec = packets[index].dts_usec;
	packet->video = packets[index].video;
	packet->keyframe = packets[index].keyframe;
}

#define SECONDS(x) ((int64_t)(x) * 1000000LL)

/* Audio packet helper. */
#define A(t) {SECONDS(t), false, false}
/* Video keyframe helper. */
#define K(t) {SECONDS(t), true, true}
/* Video inter frame helper. */
#define V(t) {SECONDS(t), true, false}

static void test_empty_buffer(void)
{
	struct replay_buffer_window window;
	CHECK(!replay_buffer_find_window(0, get_packet, NULL, 0, &window));
}

static void test_no_keyframe(void)
{
	struct test_packet packets[] = {A(0), V(1), V(2)};
	struct replay_buffer_window window;
	CHECK(!replay_buffer_find_window(3, get_packet, packets, SECONDS(5), &window));
}

static void test_full_buffer_starts_at_first_keyframe(void)
{
	struct test_packet packets[] = {A(0), K(1), V(2), V(3), K(4), V(5)};
	struct replay_buffer_window window;

	CHECK(replay_buffer_find_window(6, get_packet, packets, 0, &window));
	CHECK(window.start == 1);
	CHECK(window.end == 6);
	CHECK(window.start_dts_usec == SECONDS(1));
	CHECK(window.end_dts_usec == SECONDS(5));
}

static void test_requested_duration_is_keyframe_aligned(void)
{
	struct test_packet packets[] = {K(0), V(1), K(2), V(3), K(4), V(5), K(6), V(7)};
	struct replay_buffer_window window;

	/* End is 7s; asking for 3s targets 4s, which is a keyframe. */
	CHECK(replay_buffer_find_window(8, get_packet, packets, SECONDS(3), &window));
	CHECK(window.start == 4);
	CHECK(window.end == 8);

	/* Asking for 2.5s targets 4.5s, so the 4s keyframe is the newest one
	 * that does not start after the requested point. */
	CHECK(replay_buffer_find_window(8, get_packet, packets, SECONDS(2) + 500000, &window));
	CHECK(window.start == 4);

	/* Asking for 4.5s targets 2.5s, so the 2s keyframe is used. */
	CHECK(replay_buffer_find_window(8, get_packet, packets, SECONDS(4) + 500000, &window));
	CHECK(window.start == 2);
}

static void test_duration_longer_than_buffer_saves_available(void)
{
	struct test_packet packets[] = {K(0), V(1), V(2), K(3), V(4)};
	struct replay_buffer_window window;

	CHECK(replay_buffer_find_window(5, get_packet, packets, SECONDS(60), &window));
	CHECK(window.start == 0);
	CHECK(window.end == 5);
}

static void test_duration_between_keyframes(void)
{
	struct test_packet packets[] = {K(0), V(1), V(2), K(3), V(4), V(5), V(6), K(7), V(8)};
	struct replay_buffer_window window;

	/* End 8s, request 5s -> target 3s. */
	CHECK(replay_buffer_find_window(9, get_packet, packets, SECONDS(5), &window));
	CHECK(window.start == 3);

	/* End 8s, request 4s -> target 4s (mid-GOP), still starts at 3s. */
	CHECK(replay_buffer_find_window(9, get_packet, packets, SECONDS(4), &window));
	CHECK(window.start == 3);
}

static void test_first_keyframe_after_target_is_used(void)
{
	/* Sparse keyframes: the buffer is long, but no keyframe exists before
	 * the 6 second cutoff.  The only valid clip starts at the first
	 * keyframe, 7s. */
	struct test_packet packets[] = {K(7), V(8), V(9)};
	struct replay_buffer_window window;

	CHECK(replay_buffer_find_window(3, get_packet, packets, SECONDS(3), &window));
	CHECK(window.start == 0);
}

int main(void)
{
	test_empty_buffer();
	test_no_keyframe();
	test_full_buffer_starts_at_first_keyframe();
	test_requested_duration_is_keyframe_aligned();
	test_duration_longer_than_buffer_saves_available();
	test_duration_between_keyframes();
	test_first_keyframe_after_target_is_used();

	printf("replay buffer window tests passed\n");
	return 0;
}
