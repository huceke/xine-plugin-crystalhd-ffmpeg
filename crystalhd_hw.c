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
 * crystalhd_hw.c: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#include "crystalhd_decoder.h"
#include "crystalhd_hw.h"

#undef ALIGN
#define ALIGN(value, alignment) (((value)+(alignment-1))&~(alignment-1))

void *_aligned_malloc(size_t s, size_t alignTo) {

  char *pFull = (char*)malloc(s + alignTo + sizeof(char *));
  char *pAlligned = (char *)ALIGN(((unsigned long)pFull + sizeof(char *)), alignTo);

  *(char **)(pAlligned - sizeof(char*)) = pFull;

  return(pAlligned);
}

void _aligned_free(void *p) {
  if (!p)
    return;

  char *pFull = *(char **)(((char *)p) - sizeof(char *));
  free(pFull);
}


const char* g_DtsStatusText[] = {
        "BC_STS_SUCCESS",
        "BC_STS_INV_ARG",
        "BC_STS_BUSY",
        "BC_STS_NOT_IMPL",
        "BC_STS_PGM_QUIT",
        "BC_STS_NO_ACCESS",
        "BC_STS_INSUFF_RES",
        "BC_STS_IO_ERROR",
        "BC_STS_NO_DATA",
        "BC_STS_VER_MISMATCH",
        "BC_STS_TIMEOUT",
        "BC_STS_FW_CMD_ERR",
        "BC_STS_DEC_NOT_OPEN",
        "BC_STS_ERR_USAGE",
        "BC_STS_IO_USER_ABORT",
        "BC_STS_IO_XFR_ERROR",
        "BC_STS_DEC_NOT_STARTED",
        "BC_STS_FWHEX_NOT_FOUND",
        "BC_STS_FMT_CHANGE",
        "BC_STS_HIF_ACCESS",
        "BC_STS_CMD_CANCELLED",
        "BC_STS_FW_AUTH_FAILED",
        "BC_STS_BOOTLOADER_FAILED",
        "BC_STS_CERT_VERIFY_ERROR",
        "BC_STS_DEC_EXIST_OPEN",
        "BC_STS_PENDING",
        "BC_STS_CLK_NOCHG"
};

HANDLE crystalhd_open (int use_threading) {

	BC_STATUS res;
	uint32_t mode = DTS_PLAYBACK_MODE | DTS_LOAD_FILE_PLAY_FW | DTS_PLAYBACK_DROP_RPT_MODE |
                  DTS_DFLT_RESOLUTION(vdecRESOLUTION_720p23_976) | DTS_SKIP_TX_CHK_CPB;
  if(!use_threading) {
    mode |= DTS_SINGLE_THREADED_MODE;
  }
  HANDLE hDevice = NULL;

	res = DtsDeviceOpen(&hDevice, mode);
	if (res != BC_STS_SUCCESS) {
		printf("crystalhd_h264: ERROR: Failed to open Broadcom Crystal HD\n");
		return 0;
	}

	printf("crystalhd_h264: open device done\n");

  return hDevice;

}

HANDLE crystalhd_close(crystalhd_video_decoder_t *this, HANDLE hDevice) {

	BC_STATUS res;

	if(hDevice)  {
		res = DtsDeviceClose(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsDeviceClose\n");
  	}
	}

	xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: clsoe open done\n");

  return 0;
}

HANDLE crystalhd_stop (crystalhd_video_decoder_t *this, HANDLE hDevice) {

	BC_STATUS res;

  if(hDevice) {

		res = DtsFlushInput(hDevice, 2);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsFlushInput\n");
  	}

		res = DtsFlushRxCapture(hDevice, TRUE);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsFlushRxCapture\n");
  	}

		res = DtsStopDecoder(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsStopDecoder\n");
  	}

		res = DtsCloseDecoder(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsCloseDecoder\n");
  	}

  }

	xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: stop device done\n");

  return hDevice;
}

void crystalhd_input_format (crystalhd_video_decoder_t *this, HANDLE hDevice, BC_MEDIA_SUBTYPE mSubtype,
    int startCodeSz, uint8_t *pMetaData, uint32_t metaDataSz, int width, int height,
    int scaling_enable, int scaling_width) {

	BC_STATUS res;
  BC_INPUT_FORMAT pInputFormat;
    
  memset(&pInputFormat, 0, sizeof(BC_INPUT_FORMAT));

  pInputFormat.FGTEnable = FALSE;
  pInputFormat.MetaDataEnable = FALSE;
  pInputFormat.Progressive = TRUE;
  pInputFormat.OptFlags = 0x80000000 | vdecFrameRate23_97;
  pInputFormat.startCodeSz = startCodeSz;
  pInputFormat.mSubtype = mSubtype;
  pInputFormat.pMetaData = pMetaData;
  pInputFormat.metaDataSz = metaDataSz;
  pInputFormat.MetaDataEnable = TRUE;
  pInputFormat.width = width;
  pInputFormat.height = height;

  res = DtsSetInputFormat(hDevice, &pInputFormat);
	if (res != BC_STS_SUCCESS) {
  	xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to set input format\n");
 	}

  xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_input_format: mSubtype %d\n", mSubtype);
}

HANDLE crystalhd_start (crystalhd_video_decoder_t *this, HANDLE hDevice, 
    int startCodeSz, uint8_t *pMetaData, uint32_t metaDataSz, int width, int height,
    int scaling_enable, int scaling_width) {

	BC_STATUS res;

  if(hDevice) {

    xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_start: stream_type %d\n", this->stream_type);
    xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_start: algo %d\n", this->algo);

  	hDevice = crystalhd_stop (this, hDevice);

    if(this->decoder_reopen) {
      hDevice = crystalhd_close(this, hDevice);

      hDevice = crystalhd_open(this->use_threading);
      xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd: forced decoder reinit.\n");
    }

    crystalhd_input_format (this, hDevice, this->algo, startCodeSz, pMetaData, metaDataSz, 
        width, height, scaling_enable, scaling_width);

    if(scaling_enable) {
      BC_SCALING_PARAMS pScaleParams;
      memset(&pScaleParams, 0, sizeof(BC_SCALING_PARAMS));

      pScaleParams.sWidth = scaling_width;

      res = DtsSetScaleParams(hDevice, &pScaleParams);
    	if (res != BC_STS_SUCCESS) {
     		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed set scaling params\n");
     	}
    }

   	res = DtsSetColorSpace(hDevice, OUTPUT_MODE422_YUY2);
   	if (res != BC_STS_SUCCESS) {
   		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to set 422 mode\n");
   	}

  	res = DtsOpenDecoder(hDevice, this->stream_type);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to open decoder\n");
		  return hDevice;
  	}

  	res = DtsStartDecoder(hDevice);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to start decoder\n");
  	}

  	res = DtsStartCapture(hDevice);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to start capture\n");
  	}

  }

	xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: start device done\n");

  return hDevice;
}

BC_STATUS crystalhd_send_data(crystalhd_video_decoder_t *this, HANDLE hDevice, uint8_t *buf, uint32_t buf_len, int64_t pts) {

  BC_STATUS ret;
         
  uint8_t *sendbuf = _aligned_malloc(buf_len, 16);
  memcpy(sendbuf, buf, buf_len);

  do {
    ret = DtsProcInput(hDevice, sendbuf, buf_len, pts, 0);

    if (ret == BC_STS_BUSY) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: decoder BC_STS_BUSY\n");
      //DtsFlushInput(hDevice, 1);
      //ret = DtsProcInput(hDevice, buf, buf_len, pts, 0);
      msleep(10);
    }
  } while(ret != BC_STS_SUCCESS);

  _aligned_free(sendbuf);

  return ret;

}

uint64_t set_video_step(uint32_t frame_rate) {

  uint64_t video_step;

  /*
  switch(frame_rate) {
    case vdecFrameRate23_97:
    case 23970:
			video_step = 90000/23.976;
      break;
    case vdecFrameRate24:
    case 24000:
			video_step = 90000/24;
      break;
    case vdecFrameRate25:
    case 25000:
			video_step = 90000/25;
      break;
    case vdecFrameRate29_97:
    case 29970:
			video_step = 90000/29.97;
      break;
    case vdecFrameRate30:
    case 30000:
			video_step = 90000/30;
      break;
    case vdecFrameRate50:
    case 50000:
			video_step = 90000/50;
      break;
    case vdecFrameRate59_94:
    case 59940:
			video_step = 90000/59.94;
      break;
    case vdecFrameRate60:
    case 60000:
			video_step = 90000/60;
      break;
    case vdecFrameRate14_985:
    case 14985:
			video_step = 90000/14.985;
      break;
    case vdecFrameRate7_496:
    case 7496:
			video_step = 90000/7.496;
      break;
    case vdecFrameRateUnknown:
    default:
			video_step = 90000/23.976;
      break;
  }
  */
	switch(frame_rate) {
		case vdecRESOLUTION_720p:
		case vdecRESOLUTION_576p:
		case vdecRESOLUTION_480p:
			video_step = 90000/60;
			break;
		case vdecRESOLUTION_SD_DVD:
			video_step = 90000/50;
			break;
		case vdecRESOLUTION_PAL1:
			video_step = 90000/50;
			break;
		case vdecRESOLUTION_NTSC:
			video_step = 90000/60;
			break;
		case vdecRESOLUTION_720p50:
			video_step = 90000/50;
			break;
		case vdecRESOLUTION_1080i25:
			video_step = 90000/25;
			break;
		case vdecRESOLUTION_1080p30:
		case vdecRESOLUTION_240p30:
			video_step = 90000/30;
			break;
		case vdecRESOLUTION_1080p25:
		case vdecRESOLUTION_576p25:
		case vdecRESOLUTION_288p25:
			video_step = 90000/25;
			break;
		case vdecRESOLUTION_1080p24:
		case vdecRESOLUTION_720p24:
			video_step = 90000/24;
			break;
		case vdecRESOLUTION_1080i29_97:
		case vdecRESOLUTION_1080p29_97:
		case vdecRESOLUTION_720p29_97:
		case vdecRESOLUTION_480p29_97:
		case vdecRESOLUTION_240p29_97:
			video_step = 90000/29.97;
			break;
		case vdecRESOLUTION_1080p23_976:
		case vdecRESOLUTION_720p23_976:
		case vdecRESOLUTION_480p23_976:
		case vdecRESOLUTION_1080p0:
		case vdecRESOLUTION_576p0:
		case vdecRESOLUTION_720p0:
		case vdecRESOLUTION_480p0:
			video_step = 90000/23.976;
			break;
		case vdecRESOLUTION_1080i:
		case vdecRESOLUTION_480i:
		case vdecRESOLUTION_1080i0:
		case vdecRESOLUTION_480i0:
			video_step = 90000/25;
			break;
		case vdecRESOLUTION_720p59_94:
			video_step = 90000/59.94;
			break;
		case vdecRESOLUTION_CUSTOM:
		case vdecRESOLUTION_480p656:
		default:
			video_step = 90000/25;
			break;
	}

  printf("hansi %d\n", frame_rate);

  return video_step;
}

double set_ratio(int width, int height, uint32_t aspect_ratio) {
	lprintf("aspect_ratio %d\n", aspect_ratio);

  double ratio;

	ratio = (double)width / (double)height;
	switch(aspect_ratio) {
    case vdecAspectRatio12_11:
			ratio *= 12.0/11.0;
	  case vdecAspectRatio10_11:
    	ratio *= 10.0/11.0;
    	break;
    case vdecAspectRatio16_11:
      ratio *= 16.0/11.0;
      break;
    case vdecAspectRatio40_33:
      ratio *= 40.0/33.0;
      break;
    case vdecAspectRatio24_11:
      ratio *= 24.0/11.0;
      break;
    case vdecAspectRatio20_11:
      ratio *= 20.0/11.0;
      break;
    case vdecAspectRatio32_11:
      ratio *= 32.0/11.0;
      break;
    case vdecAspectRatio80_33:
      ratio *= 80.0/33.0;
      break;
    case vdecAspectRatio18_11:
      ratio *= 18.0/11.0;
      break;
    case vdecAspectRatio15_11:
      ratio *= 15.0/11.0;
      break;
    case vdecAspectRatio64_33:
      ratio *= 64.0/33.0;
      break;
    case vdecAspectRatio160_99:
      ratio *= 160.0/99.0;
      break;
    case vdecAspectRatio4_3:
      ratio *= 4.0/3.0;
      break;
    case vdecAspectRatio16_9:
      ratio *= 16.0 / 9.0;
      break;
    case vdecAspectRatio221_1:
      ratio *= 2.0/1.0;
      break;
		case vdecAspectRatioUnknown:
		case vdecAspectRatioOther:
		default:
			ratio = (double)width / (double)height;
			break;
	}

  return ratio;
}
