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
 * crystalhd_hw.h: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#ifndef CRYSTALHD_HW_H
#define CRYSTALHD_HW_H

#include "crystalhd_decoder.h"

HANDLE crystalhd_open(int use_threading);
/*
HANDLE crystalhd_close(xine_t *xine, HANDLE hDevice);
*/
HANDLE crystalhd_start(crystalhd_video_decoder_t *this, HANDLE hDevice, 
     int startCodeSz, uint8_t *pMetaData, uint32_t metaDataSz, int width, int height,
     int scaling_enable, int scaling_width);
void crystalhd_input_format (crystalhd_video_decoder_t *this, HANDLE hDevice, BC_MEDIA_SUBTYPE mSubtype,
    int startCodeSz, uint8_t *pMetaData, uint32_t metaDataSz, int width, int height,
    int scaling_enable, int scaling_width);
HANDLE crystalhd_stop(crystalhd_video_decoder_t *this, HANDLE hDevice);
HANDLE crystalhd_close(crystalhd_video_decoder_t *this, HANDLE hDevice);
BC_STATUS crystalhd_send_data(crystalhd_video_decoder_t *this, HANDLE hDevice, uint8_t *buf, uint32_t buf_len, int64_t pts);
uint64_t set_video_step(uint32_t frame_rate);
double set_ratio(int width, int height, uint32_t aspect_ratio);
void *_aligned_malloc(size_t s, size_t alignTo);
void _aligned_free(void *p);

#endif
