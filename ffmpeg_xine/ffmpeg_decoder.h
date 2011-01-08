/*
 * Copyright (C) 2001-2005 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

#ifndef HAVE_XINE_DECODER_H
#define HAVE_XINE_DECODER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <avcodec.h>
#else
#  include <libavcodec/avcodec.h>
#endif

#if LIBAVCODEC_VERSION_MAJOR > 51
#define bits_per_sample bits_per_coded_sample
#endif

typedef struct ff_codec_s {
  uint32_t          type;
  enum CodecID      id;
  const char       *name;
} ff_codec_t;

void *init_audio_plugin (xine_t *xine, void *data);
void *init_video_plugin (xine_t *xine, void *data);

extern decoder_info_t dec_info_ffmpeg_video;
extern decoder_info_t dec_info_ffmpeg_wmv8;
extern decoder_info_t dec_info_ffmpeg_wmv9;
extern decoder_info_t dec_info_ffmpeg_audio;

extern pthread_once_t once_control;
void init_once_routine(void);

extern pthread_mutex_t ffmpeg_lock;

#endif
