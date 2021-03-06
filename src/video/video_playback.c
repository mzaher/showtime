/*
 *  Playback of video
 *  Copyright (C) 2007-2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "video_playback.h"
#include "event.h"
#include "media.h"
#include "backend/backend.h"
#include "notifications.h"

/**
 *
 */
static void *
video_player_idle(void *aux)
{
  video_playback_t *vp = aux;
  int run = 1;
  event_t *e = NULL, *next;
  media_pipe_t *mp = vp->vp_mp;
  char errbuf[256];
  prop_t *errprop = prop_ref_inc(prop_create(mp->mp_prop_root, "error"));

  while(run) {

    if(e == NULL)
      e = mp_dequeue_event(mp);
    

    if(event_is_type(e, EVENT_PLAY_URL)) {
      prop_set_void(errprop);
      event_playurl_t *ep = (event_playurl_t *)e;
      int flags = 0;
      if(ep->primary)
	flags |= BACKEND_VIDEO_PRIMARY;
      if(ep->no_audio)
	flags |= BACKEND_VIDEO_NO_AUDIO;

      next = backend_play_video(ep->url, mp, flags, ep->priority,
				errbuf, sizeof(errbuf));

      if(next == NULL) {
	notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nError: %s", 
		   ep->url, errbuf);
	prop_set_string(errprop, errbuf);
      }
      event_release(e);
      e = next;
      continue;

    } else if(event_is_type(e, EVENT_EXIT)) {
      event_release(e);
      break;
    }
    event_release(e);
    e = NULL;
  }
  prop_ref_dec(errprop);
  return NULL;
}

/**
 *
 */
video_playback_t *
video_playback_create(media_pipe_t *mp)
{
  video_playback_t *vp = calloc(1, sizeof(video_playback_t));
  vp->vp_mp = mp;
  hts_thread_create_joinable("video player", 
			     &vp->vp_thread, video_player_idle, vp,
			     THREAD_PRIO_NORMAL);
  return vp;
}


/**
 *
 */
void
video_playback_destroy(video_playback_t *vp)
{
  event_t *e = event_create_type(EVENT_EXIT);

  mp_enqueue_event(vp->vp_mp, e);
  event_release(e);

  hts_thread_join(&vp->vp_thread);
  free(vp);
}
