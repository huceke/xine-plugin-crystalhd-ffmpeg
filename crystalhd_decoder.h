/*
 * Copyright (C) 2009 Edgar Hucek
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
 *
 */

#ifndef CRYSTALHD_H
#define CRYSTALHD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#define XINE_ENGINE_INTERNAL

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#ifndef __LINUX_USER__
#define __LINUX_USER__
#endif

#include <bc_dts_types.h>
#include <bc_dts_defs.h>
#include <libcrystalhd_if.h>

#include <semaphore.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

extern HANDLE hDevice;

extern const char* g_DtsStatusText[];

typedef struct decoder_buffer_s {
	uint8_t		*bytestream;
 	uint32_t	bytestream_bytes;
	uint64_t   pts;
} decoder_buffer_t;

typedef struct image_buffer_s {
	uint8_t		*image;
 	uint32_t	image_bytes;
	int				width;
	int				height;
	uint64_t  pts;
  double    ratio;
	uint32_t  video_step;
	uint32_t  flags;
	int			  interlaced;
	uint32_t	picture_number;
  uint32_t  stride;
} image_buffer_t;

/* MGED Picture */
typedef struct {
  int                     slices_count, slices_count2;
  uint8_t                 *slices;
  int                     slices_size;
  int                     slices_pos, slices_pos_top;

  int                     progressive_frame;
  int                     state;
  int                     picture_structure;
} picture_mpeg_t;


typedef struct {
  video_decoder_class_t   decoder_class;
} crystalhd_video_class_t;

#define VIDEOBUFSIZE        (128*1024)

typedef struct CHD_CODEC_PARAMS {
  uint8_t   *sps_pps_buf;
  uint32_t  sps_pps_size;
  uint8_t   nal_size_bytes;
} CHD_CODEC_PARAMS;

typedef struct chd_bitstream_ctx {
  uint8_t  length_size;
  uint8_t  first_idr;
  uint8_t *sps_pps_data;
  uint32_t size;
} chd_bitstream_ctx;

typedef uint32_t BCM_STREAM_TYPE;
typedef uint32_t BCM_VIDEO_ALGO;

typedef struct crystalhd_video_decoder_s {
	video_decoder_t   video_decoder;  /* parent video decoder structure */

	crystalhd_video_class_t *class;
	xine_stream_t    *stream;
	xine_t            *xine;

	/* these are traditional variables in a video decoder object */
	double            ratio;
	uint32_t          video_step;
  uint32_t          reported_video_step;  /* frame duration in pts units */
  int               aspect_ratio_prio;
  int64_t           pts;
  int               reset;

	int								width;
	int								height;
	int								y_size;
	int								uv_size;

	int								interlaced;
	int								last_image;

	xine_list_t       *image_buffer;

	pthread_t         rec_thread;
	int								rec_thread_stop;
	pthread_mutex_t		rec_mutex;

  int               codec_type;

  int               scaling_enable;
  int               scaling_width;
  int               use_threading;
  int               extra_logging;
  int               decoder_reopen;
  int               decoder_25p;
  int               decoder_25p_drop;

  AVCodecContext    *av_context;
  AVFrame           *av_frame;
  AVCodec           *av_codec;
  uint8_t           decoder_init:1;
  uint8_t           decoder_init_mode:1;
  AVCodecParserContext *av_parser;
  int               av_got_picture;
  xine_bmiheader    bih;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               bitstream_convert;

  uint32_t          m_sps_pps_size;
  chd_bitstream_ctx m_sps_pps_context;
  int               m_convert_bitstream;

  CHD_CODEC_PARAMS  m_chd_params;

  BCM_STREAM_TYPE stream_type;
  BCM_VIDEO_ALGO algo;

} crystalhd_video_decoder_t;

void *crystalhd_video_rec_thread (void *this_gen);
void crystalhd_decode_package (uint8_t *buf, uint32_t size);
void set_video_params (crystalhd_video_decoder_t *this);

#endif
