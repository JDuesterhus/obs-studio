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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Small, dependency-free helper used to choose which part of the replay
 * buffer is written when the user asks for a specific duration.
 *
 * The buffer stores encoded packets and is purged from the front at video
 * keyframes, so a saved clip must also start at a video keyframe or the
 * decoder has no reference frame for the first packets.  The window helper
 * therefore picks the newest keyframe at or before the requested start.  If
 * the requested duration reaches further back than the buffer does, the
 * oldest available keyframe is used instead, which gracefully saves the
 * footage that is available.
 *
 * It lives in its own header so it can be unit tested without libobs,
 * FFmpeg, or any platform code.
 */

struct replay_buffer_window_packet {
	int64_t dts_usec;
	bool video;
	bool keyframe;
};

struct replay_buffer_window {
	/* Index of the first packet to write. */
	size_t start;
	/* One past the last packet to write. */
	size_t end;
	int64_t start_dts_usec;
	int64_t end_dts_usec;
};

/*
 * Provides packet `index` to the window finder.  Using a callback keeps the
 * helper independent of the replay buffer's deque and avoids allocating a
 * second copy of the packet metadata on save.
 */
typedef void (*replay_buffer_window_get_packet)(void *context, size_t index,
						struct replay_buffer_window_packet *packet);

static inline bool replay_buffer_find_window(size_t count, replay_buffer_window_get_packet get_packet, void *context,
					     int64_t duration_usec, struct replay_buffer_window *window)
{
	struct replay_buffer_window_packet pkt;
	size_t first_keyframe = SIZE_MAX;
	size_t newest_keyframe_before_start = SIZE_MAX;
	int64_t cutoff;
	int64_t end_dts_usec;

	if (!count || !get_packet || !window) {
		return false;
	}

	get_packet(context, count - 1, &pkt);
	end_dts_usec = pkt.dts_usec;

	/*
	 * A non-positive duration means "the whole buffer".  Setting the cutoff
	 * to INT64_MIN makes the search below pick the first keyframe.
	 */
	if (duration_usec > 0 && end_dts_usec > INT64_MIN + duration_usec) {
		cutoff = end_dts_usec - duration_usec;
	} else {
		cutoff = INT64_MIN;
	}

	for (size_t i = 0; i < count; i++) {
		get_packet(context, i, &pkt);
		if (!pkt.video || !pkt.keyframe) {
			continue;
		}

		if (first_keyframe == SIZE_MAX) {
			first_keyframe = i;
		}

		if (pkt.dts_usec <= cutoff) {
			newest_keyframe_before_start = i;
		}
	}

	/*
	 * No keyframe at or before the requested start means the requested
	 * duration is longer than the retained buffer (or falls between
	 * keyframes).  Start at the oldest keyframe so what is available is
	 * still saved.
	 */
	if (newest_keyframe_before_start == SIZE_MAX) {
		newest_keyframe_before_start = first_keyframe;
	}

	if (newest_keyframe_before_start == SIZE_MAX) {
		return false;
	}

	get_packet(context, newest_keyframe_before_start, &pkt);

	window->start = newest_keyframe_before_start;
	window->end = count;
	window->start_dts_usec = pkt.dts_usec;
	window->end_dts_usec = end_dts_usec;
	return true;
}
