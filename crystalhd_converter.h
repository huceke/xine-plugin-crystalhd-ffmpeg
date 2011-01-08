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

#ifndef CRYSTALHD_CONVERTER_H
#define CRYSTALHD_CONVERTER_H

#include "crystalhd_decoder.h"

int extract_sps_pps_from_avcc(crystalhd_video_decoder_t *this, int extradata_size, void *extradata);
int bitstream_convert_init(crystalhd_video_decoder_t *this, void *in_extradata, int in_extrasize);
int bitstream_convert(crystalhd_video_decoder_t *this, uint8_t* pData, int iSize, uint8_t **poutbuf, int *poutbuf_size);
void bitstream_alloc_and_copy(crystalhd_video_decoder_t *this, uint8_t **poutbuf, int *poutbuf_size,
    const uint8_t *sps_pps, uint32_t sps_pps_size, const uint8_t *in, uint32_t in_size);

#endif
