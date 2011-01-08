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

#include "crystalhd_decoder.h"
#include "crystalhd_converter.h"

int extract_sps_pps_from_avcc(crystalhd_video_decoder_t *this, int extradata_size, void *extradata)
{
  uint8_t *data = (uint8_t*)extradata;
  uint32_t data_size = extradata_size;
  int profile;
  unsigned int nal_size;
  unsigned int num_sps, num_pps;
  unsigned int i;

  this->m_chd_params.sps_pps_size = 0;

  profile = (data[1] << 16) | (data[2] << 8) | data[3];

  this->m_chd_params.nal_size_bytes = (data[4] & 0x03) + 1;

  num_sps = data[5] & 0x1f;

  data += 6;
  data_size -= 6;

  for (i = 0; i < num_sps; i++)
  {
    if (data_size < 2)
      return 0;

    nal_size = (data[0] << 8) | data[1];
    data += 2;
    data_size -= 2;

    if (data_size < nal_size)
			return 0;

    this->m_chd_params.sps_pps_buf[0] = 0;
    this->m_chd_params.sps_pps_buf[1] = 0;
    this->m_chd_params.sps_pps_buf[2] = 0;
    this->m_chd_params.sps_pps_buf[3] = 1;

    this->m_chd_params.sps_pps_size += 4;

    memcpy(this->m_chd_params.sps_pps_buf + this->m_chd_params.sps_pps_size, data, nal_size);
    this->m_chd_params.sps_pps_size += nal_size;

    data += nal_size;
    data_size -= nal_size;
  }

  if (data_size < 1)
    return 0;

  num_pps = data[0];
  data += 1;
  data_size -= 1;

  for (i = 0; i < num_pps; i++)
  {
    if (data_size < 2)
      return 0;

    nal_size = (data[0] << 8) | data[1];
    data += 2;
    data_size -= 2;

    if (data_size < nal_size)
      return 0;

    this->m_chd_params.sps_pps_buf[this->m_chd_params.sps_pps_size+0] = 0;
    this->m_chd_params.sps_pps_buf[this->m_chd_params.sps_pps_size+1] = 0;
    this->m_chd_params.sps_pps_buf[this->m_chd_params.sps_pps_size+2] = 0;
    this->m_chd_params.sps_pps_buf[this->m_chd_params.sps_pps_size+3] = 1;

    this->m_chd_params.sps_pps_size += 4;

    memcpy(this->m_chd_params.sps_pps_buf + this->m_chd_params.sps_pps_size, data, nal_size);
    this->m_chd_params.sps_pps_size += nal_size;

    data += nal_size;
    data_size -= nal_size;
  }

  return 1;
}

int bitstream_convert_init(crystalhd_video_decoder_t *this, void *in_extradata, int in_extrasize)
{
  // based on h264_mp4toannexb_bsf.c (ffmpeg)
  // which is Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
  // and Licensed GPL 2.1 or greater

  this->m_sps_pps_size = 0;
  this->m_sps_pps_context.sps_pps_data = NULL;
  
  // nothing to filter
  if (!in_extradata || in_extrasize < 6)
    return 0;

  uint16_t unit_size;
  uint32_t total_size = 0;
  uint8_t *out = NULL, unit_nb, sps_done = 0;
  const uint8_t *extradata = (uint8_t*)in_extradata + 4;
  static const uint8_t nalu_header[4] = {0, 0, 0, 1};

  // retrieve length coded size
  this->m_sps_pps_context.length_size = (*extradata++ & 0x3) + 1;
  if (this->m_sps_pps_context.length_size == 3)
    return 0;

  // retrieve sps and pps unit(s)
  unit_nb = *extradata++ & 0x1f;  // number of sps unit(s)
  if (!unit_nb)
  {
    unit_nb = *extradata++;       // number of pps unit(s)
    sps_done++;
  }
  while (unit_nb--)
  {
    unit_size = extradata[0] << 8 | extradata[1];
    total_size += unit_size + 4;
    if ( (extradata + 2 + unit_size) > ((uint8_t*)in_extradata + in_extrasize) )
    {
      free(out);
      return 0;
    }
    out = (uint8_t*)realloc(out, total_size);
    if (!out)
      return 0;

    memcpy(out + total_size - unit_size - 4, nalu_header, 4);
    memcpy(out + total_size - unit_size, extradata + 2, unit_size);
    extradata += 2 + unit_size;

    if (!unit_nb && !sps_done++)
      unit_nb = *extradata++;     // number of pps unit(s)
  }

  this->m_sps_pps_context.sps_pps_data = out;
  this->m_sps_pps_context.size = total_size;
  this->m_sps_pps_context.first_idr = 1;

  return 1;
}

int bitstream_convert(crystalhd_video_decoder_t *this, uint8_t* pData, int iSize, uint8_t **poutbuf, int *poutbuf_size)
{
  // based on h264_mp4toannexb_bsf.c (ffmpeg)
  // which is Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
  // and Licensed GPL 2.1 or greater

  uint8_t *buf = pData;
  uint32_t buf_size = iSize;
  uint8_t  unit_type;
  int32_t  nal_size;
  uint32_t cumul_size = 0;
  const uint8_t *buf_end = buf + buf_size;

  do
  {
    if (buf + this->m_sps_pps_context.length_size > buf_end)
      goto fail;

    if (this->m_sps_pps_context.length_size == 1)
      nal_size = buf[0];
    else if (this->m_sps_pps_context.length_size == 2)
      nal_size = buf[0] << 8 | buf[1];
    else
      nal_size = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

    buf += this->m_sps_pps_context.length_size;
    unit_type = *buf & 0x1f;

    if (buf + nal_size > buf_end || nal_size < 0)
      goto fail;

    // prepend only to the first type 5 NAL unit of an IDR picture
    if (this->m_sps_pps_context.first_idr && unit_type == 5)
    {
      bitstream_alloc_and_copy(this, poutbuf, poutbuf_size,
        this->m_sps_pps_context.sps_pps_data, this->m_sps_pps_context.size, buf, nal_size);
      this->m_sps_pps_context.first_idr = 0;
    }
    else
    {
      bitstream_alloc_and_copy(this, poutbuf, poutbuf_size, NULL, 0, buf, nal_size);
      if (!this->m_sps_pps_context.first_idr && unit_type == 1)
          this->m_sps_pps_context.first_idr = 1;
    }

    buf += nal_size;
    cumul_size += nal_size + this->m_sps_pps_context.length_size;
  } while (cumul_size < buf_size);

  return 1;

fail:
  free(*poutbuf);
  *poutbuf = NULL;
  *poutbuf_size = 0;
  return 0;
}

void bitstream_alloc_and_copy(
  crystalhd_video_decoder_t *this,
  uint8_t **poutbuf,      int *poutbuf_size,
  const uint8_t *sps_pps, uint32_t sps_pps_size,
  const uint8_t *in,      uint32_t in_size)
{
  // based on h264_mp4toannexb_bsf.c (ffmpeg)
  // which is Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
  // and Licensed GPL 2.1 or greater

  #define CHD_WB32(p, d) { \
    ((uint8_t*)(p))[3] = (d); \
    ((uint8_t*)(p))[2] = (d) >> 8; \
    ((uint8_t*)(p))[1] = (d) >> 16; \
    ((uint8_t*)(p))[0] = (d) >> 24; }

  uint32_t offset = *poutbuf_size;
  uint8_t nal_header_size = offset ? 3 : 4;

  *poutbuf_size += sps_pps_size + in_size + nal_header_size;
  *poutbuf = (uint8_t*)realloc(*poutbuf, *poutbuf_size);
  if (sps_pps)
    memcpy(*poutbuf + offset, sps_pps, sps_pps_size);

  memcpy(*poutbuf + sps_pps_size + nal_header_size + offset, in, in_size);
  if (!offset)
  {
    CHD_WB32(*poutbuf + sps_pps_size, 1);
  }
  else
  {
    (*poutbuf + offset + sps_pps_size)[0] = 0;
    (*poutbuf + offset + sps_pps_size)[1] = 0;
    (*poutbuf + offset + sps_pps_size)[2] = 1;
  }
}

