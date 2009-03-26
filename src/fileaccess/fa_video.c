/*
 *  Playback of video
 *  Copyright (C) 2007-2008 Andreas �man
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

#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "fa_video.h"
#include "event.h"
#include "media.h"
#include "fa_probe.h"
#include "fileaccess.h"
#include "dvd/dvd.h"
#include "notifications.h"

/**
 *
 */
static int64_t
rescale(AVFormatContext *fctx, int64_t ts, int si)
{
  if(ts == AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;

  return av_rescale_q(ts, fctx->streams[si]->time_base, AV_TIME_BASE_Q);
}


/**
 * Thread for reading from lavf and sending to lavc
 */
static event_t *
video_player_loop(AVFormatContext *fctx, codecwrap_t **cwvec, media_pipe_t *mp)
{
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVCodecContext *ctx;
  AVPacket pkt;
  int r, si;
  event_t *e;
  event_seek_t *es;
  int64_t ts;
  int hold = 0;

  while(1) {
    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if((r = av_read_frame(fctx, &pkt)) < 0) {

	if(r == AVERROR_EOF &&
	   !av_seek_frame(fctx, -1, fctx->start_time, AVSEEK_FLAG_BACKWARD)) {

	  mp_wait(mp,
		  mp->mp_audio.mq_stream != -1, 
		  mp->mp_video.mq_stream != -1);

	  mp_send_cmd(mp, &mp->mp_video, MB_FLUSH);
	  mp_send_cmd(mp, &mp->mp_audio, MB_FLUSH);
	  
	  notify_add(NOTIFY_INFO, NULL, 5, "Video ended, restarting");
	  continue;
	}

	notify_add(NOTIFY_INFO, NULL, 5, "Video read error");
	e = NULL;
	break;
      }

      si = pkt.stream_index;

      ctx = cwvec[si] ? cwvec[si]->codec_ctx : NULL;

      if(ctx != NULL && si == mp->mp_video.mq_stream) {
	/* Current video stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_VIDEO;
	mq = &mp->mp_video;
	
      } else if(ctx != NULL && si == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }

      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      mb->mb_duration = rescale(fctx, pkt.duration, si);

      mb->mb_cw = wrap_codec_ref(cwvec[si]);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      mb->mb_data = pkt.data;
      pkt.data = NULL;

      mb->mb_size = pkt.size;
      pkt.size = 0;

      if(mb->mb_pts != AV_NOPTS_VALUE && mb->mb_data_type == MB_AUDIO)
	mb->mb_time = mb->mb_pts - fctx->start_time;
      else
	mb->mb_time = AV_NOPTS_VALUE;

      av_free_packet(&pkt);
    }

    /*
     * Try to send the buffer.  If mb_enqueue() returns something we
     * catched an event instead of enqueueing the buffer. In this case
     * 'mb' will be left untouched.
     */

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    switch(e->e_type) {

    case EVENT_PLAYPAUSE:
    case EVENT_PLAY:
    case EVENT_PAUSE:
      hold = mp_update_hold_by_event(hold, e->e_type);
      mp_send_cmd_head(mp, &mp->mp_video, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      break;

    case EVENT_SEEK:
      es = (event_seek_t *)e;
      
      ts = es->ts + fctx->start_time;

      if(ts < fctx->start_time)
	ts = fctx->start_time;

      r = av_seek_frame(fctx, -1, ts, AVSEEK_FLAG_BACKWARD);

      mp_flush(mp);

      if(mb != NULL) {
	media_buf_free(mb);
	mb = NULL;
      }
      break;

    default:
      break;

    case EVENT_EXIT:
    case EVENT_PLAY_URL:
      goto out;

    }
    event_unref(e);
  }

 out:
  if(mb != NULL)
    media_buf_free(mb);
  return e;
}

/**
 *
 */
event_t *
be_file_playvideo(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  formatwrap_t *fw;
  int i;
  char faurl[1000];
  codecwrap_t **cwvec;
  event_t *e;
  struct stat buf;
  fa_handle_t *fh;

  if(fa_stat(url, &buf, errbuf, errlen))
    return NULL;
  
  /**
   * Is it a DVD ?
   */

  if(S_ISDIR(buf.st_mode)) {
    
    if(fa_probe_dir(NULL, url) == CONTENT_DVD)
      goto isdvd;

    return NULL;
  }

  if((fh = fa_open(url, errbuf, errlen)) == NULL)
    return NULL;

  if(fa_probe_iso(NULL, fh) == 0) {
    fa_close(fh);
  isdvd:
#if ENABLE_DVDNAV
    return dvd_play(url, mp, errbuf, errlen);
#else
    snprintf(errbuf, errlen, "DVD playback is not supported");
    return NULL;
#endif
  }
  fa_close(fh);


  /**
   * Open input file
   */
  snprintf(faurl, sizeof(faurl), "showtime:%s", url);
  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    snprintf(errbuf, errlen, "Unable to open input file %s", url);
    return NULL;
  }

  fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    snprintf(errbuf, errlen, "Unable to find stream info");
    return NULL;
  }

  /**
   * Update property metadata
   */

  fa_lavf_load_meta(mp->mp_prop_metadata, fctx, faurl);

  /**
   * Init codec contexts
   */
  cwvec = alloca(fctx->nb_streams * sizeof(void *));
  memset(cwvec, 0, sizeof(void *) * fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx);

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO)
      mp->mp_video.mq_stream = i;

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO)
      mp->mp_audio.mq_stream = i;

    cwvec[i] = wrap_codec_create(ctx->codec_id,
					  ctx->codec_type, 0, fw, ctx, 0);
  }

  /**
   * Restart playback at last position
   */

  mp_prepare(mp, MP_GRAB_AUDIO);

  e = video_player_loop(fctx, cwvec, mp);

  mp_flush(mp);

  mp_hibernate(mp);

  for(i = 0; i < fctx->nb_streams; i++)
    if(cwvec[i] != NULL)
      wrap_codec_deref(cwvec[i]);

  wrap_format_deref(fw);
  return e;
}