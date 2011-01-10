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
 * crystalhd_decoder.c: Main video decoder routines
 */

#define LOG

#include "crystalhd_decoder.h"
#include "crystalhd_converter.h"
#include "crystalhd_hw.h"

static void crystalhd_video_flush (video_decoder_t *this);
static void crystalhd_video_destroy_workers(crystalhd_video_decoder_t *this);
static void crystalhd_video_setup_workers(crystalhd_video_decoder_t *this);
static void crystalhd_video_clear_worker_buffers(crystalhd_video_decoder_t *this);

HANDLE hDevice = 0;

int __nsleep(const struct  timespec *req, struct timespec *rem) {
  struct timespec temp_rem;
  if(nanosleep(req,rem)==-1)
    __nsleep(rem,&temp_rem);

  return 1;
}

int msleep(unsigned long milisec) {
  struct  timespec req={0},rem={0};
  time_t sec=(int)(milisec/1000);
  milisec=milisec-(sec*1000);
  req.tv_sec=sec;
  req.tv_nsec=milisec*1000000L;
  __nsleep(&req,&rem);
  return 1;
}

void crystalhd_decode_package (uint8_t *buf, uint32_t size) {
	int i;

	if(size == 0) return;

	printf("Decode data: \n");

	for(i = 0; i < ((size < 80) ? size : 80); i++) {
		printf("%02x ", ((uint8_t*)buf)[i]);
		if((i+1) % 40 == 0)
			printf("\n");
	}
	printf("\n...\n");
}

pthread_once_t once_control = PTHREAD_ONCE_INIT;
pthread_mutex_t ffmpeg_lock;

void init_once_routine(void) {
  pthread_mutex_init(&ffmpeg_lock, NULL);
  avcodec_init();
  avcodec_register_all();
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

void print_setup(crystalhd_video_decoder_t *this) {
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->width %d\n", this->width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->height %d\n", this->height);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->interlaced %d\n", this->interlaced);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->video_step %d\n", this->video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->reported_video_step %d\n", this->reported_video_step);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "this->ratio %f\n", this->ratio);
}

void set_video_params (crystalhd_video_decoder_t *this) {

	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
	_x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
	_x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "crystalhd decoder" );

	xine_event_t event;
	xine_format_change_data_t data;
	event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
	event.stream = this->stream;
	event.data = &data;
	event.data_length = sizeof(data);
	data.width = this->width;
	data.height = this->height;
	data.aspect = this->ratio;
	data.pan_scan = 1;
	xine_event_send( this->stream, &event );

	print_setup(this);
} 

static void crystalhd_video_render (crystalhd_video_decoder_t *this, image_buffer_t *_img) {

	xine_list_iterator_t ite = NULL;
 	image_buffer_t *img	= NULL;

  if(this->use_threading) {
	  ite = xine_list_front(this->image_buffer);
    if(ite!= NULL) {
  	  img	= xine_list_get_value(this->image_buffer, ite);
		  xine_list_remove(this->image_buffer, ite);
    }
  } else {
    img = _img;
  }

 	if(img != NULL && img->image_bytes > 0) {
    vo_frame_t	*vo_img;

   	vo_img = this->stream->video_out->get_frame (this->stream->video_out,
                      img->width, (img->interlaced) ? img->height / 2 : img->height, img->ratio, 
               				XINE_IMGFMT_YUY2, VO_BOTH_FIELDS | VO_PAN_SCAN_FLAG | this->reset);

    this->reset = 0;

    //printf("img->image_bytes %d width %d height %d interlaced %d\n", img->image_bytes, img->width, img->height, img->interlaced);

   	yuy2_to_yuy2(
    		  	img->image, img->width * 2,
 		      	vo_img->base[0], vo_img->pitches[0],
 		  	    img->width, (img->interlaced) ? img->height / 2 : img->height);
   	vo_img->pts			 = img->pts;
   	vo_img->duration = img->video_step;
    vo_img->bad_frame = 0;

   	vo_img->draw(vo_img, this->stream);

   	vo_img->free(vo_img);
  }

  if(img != NULL && this->use_threading) {
  	free(img->image);
    free(img);
  }
}

void* crystalhd_video_rec_thread (void *this_gen) {
	crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

	BC_STATUS         ret = BC_STS_ERROR;
	BC_DTS_STATUS     pStatus;
  BC_DTS_PROC_OUT		procOut;
	//unsigned char   	*transferbuff = NULL;
	int								decoder_timeout = 16;
  static uint64_t   pts = 0;
  //int               mutex_lock = 0;

	while(!this->rec_thread_stop) {
	
    if(!this->decoder_init) {
      if(!this->use_threading) {
        return NULL;
      }
      msleep(10);
      continue;
    }

    /*
    if(this->use_threading) {
      mutex_lock = pthread_mutex_trylock(&this->rec_mutex);
      if(mutex_lock == EBUSY) {
        continue;
      }
    }
    */

		/* read driver status. we need the frame ready count from it */
    memset(&pStatus, 0, sizeof(BC_DTS_STATUS));
    ret = DtsGetDriverStatus(hDevice, &pStatus);

		if( ret == BC_STS_SUCCESS && pStatus.ReadyListCount) {

			memset(&procOut, 0, sizeof(BC_DTS_PROC_OUT));

		  if(this->interlaced) {
			  procOut.PoutFlags |= BC_POUT_FLAGS_INTERLACED;
		  }
      procOut.b422Mode = OUTPUT_MODE422_YUY2;

      if(this->use_threading) {
			
        /* setup frame transfer structure */
        /*
			  procOut.PicInfo.width = this->width;
			  procOut.PicInfo.height = this->height;
			  procOut.YbuffSz = this->y_size/4;
			  procOut.UVbuffSz = this->uv_size/4;
			  procOut.PoutFlags = BC_POUT_FLAGS_SIZE;
	
			  procOut.PicInfo.picture_number = 0;
	
			  if(transferbuff == NULL) {
				  transferbuff = _aligned_malloc(this->y_size, 16);
		  	}
			  procOut.Ybuff = transferbuff;

			  procOut.PoutFlags = procOut.PoutFlags & 0xff;
  			ret = DtsProcOutput(hDevice, decoder_timeout, &procOut);
        */
	
  			ret = DtsProcOutputNoCopy(hDevice, decoder_timeout, &procOut);

      } else {	
  			ret = DtsProcOutputNoCopy(hDevice, decoder_timeout, &procOut);
      }

			/* print statistics */
			switch (ret) {
				case BC_STS_NO_DATA:
					break;
	     	case BC_STS_FMT_CHANGE:
	       	if ((procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) && 
							(procOut.PoutFlags & BC_POUT_FLAGS_FMT_CHANGE) ) {
	
						this->interlaced = (procOut.PicInfo.flags & VDEC_FLAG_INTERLACED_SRC ? 1 : 0);
						//this->interlaced = (procOut.PicInfo.flags & VDEC_FLAG_FIELDPAIR ? 1 : 0);
	
	       		this->width = procOut.PicInfo.width;
						this->height = procOut.PicInfo.height;
						if(this->height == 1088) this->height = 1080;

						if(this->interlaced) {
							this->y_size = this->width * this->height;
						} else {
							this->y_size = this->width * this->height * 2;
						}

						this->uv_size = 0;
	
						decoder_timeout = 16;
				
						//this->ratio = set_ratio(this->width, this->height, procOut.PicInfo.aspect_ratio);
            //this->video_step = set_video_step(procOut.PicInfo.frame_rate);
            set_video_params(this);
            this->last_image = 0;
            pts = 0;
	   	   	}
					break;
				case BC_STS_SUCCESS:
	     	 	if ((procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) &&
              procOut.PicInfo.timeStamp == 0) {
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG,"crystalhd_video: timeStamp == 0 picture_number %d\n", procOut.PicInfo.picture_number);
          } else if (procOut.PoutFlags & BC_POUT_FLAGS_PIB_VALID) {
	
						if(this->last_image == 0) {
							this->last_image = procOut.PicInfo.picture_number;
						}

            if((procOut.PicInfo.picture_number - this->last_image) > 0 ) {

              if(this->extra_logging) {
                fprintf(stderr,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d timeStamp %" PRId64 " YbuffSz %d YBuffDoneSz %d UVbuffSz %d UVBuffDoneSz %d\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									procOut.PicInfo.picture_number, 
									procOut.PicInfo.picture_number - this->last_image,
                  procOut.PicInfo.timeStamp,
                  procOut.YbuffSz, procOut.YBuffDoneSz, 
                  procOut.UVbuffSz, procOut.UVBuffDoneSz);
              }

              if((procOut.PicInfo.picture_number - this->last_image) > 1) {
							  xprintf(this->xine, XINE_VERBOSITY_NONE,"ReadyListCount %d FreeListCount %d PIBMissCount %d picture_number %d gap %d timeStamp %" PRId64 "\n",
									pStatus.ReadyListCount, pStatus.FreeListCount, pStatus.PIBMissCount, 
									procOut.PicInfo.picture_number, 
									procOut.PicInfo.picture_number - this->last_image,
                  procOut.PicInfo.timeStamp);
              }

							if(procOut.PicInfo.picture_number != this->last_image) {
								this->last_image = procOut.PicInfo.picture_number;
							}

							image_buffer_t *img = NULL;
              image_buffer_t _img;

							/* allocate new image buffer and push it to the image list */
              if(this->use_threading) {
							  img = malloc(sizeof(image_buffer_t));
							  //img->image = transferbuff;
							  //img->image_bytes = procOut.YbuffSz;
							  //img->image_bytes = procOut.YBuffDoneSz + procOut.UVbuffSz;
							  img->image_bytes = this->width * this->height * 2;
							  img->image = malloc(img->image_bytes);
                xine_fast_memcpy(img->image, procOut.Ybuff, img->image_bytes);
              } else {
                memset(&_img, 0 , sizeof(image_buffer_t));
                img = &_img;
							  img->image = procOut.Ybuff;
							  img->image_bytes = procOut.YBuffDoneSz + procOut.UVbuffSz;
              }

							img->width = this->width;
							img->height = this->height;
              if(procOut.PicInfo.timeStamp) {
                pts = procOut.PicInfo.timeStamp;
              }
							//img->pts = procOut.PicInfo.timeStamp;
							img->pts = pts;
						 	img->video_step = this->video_step;
						 	img->ratio = this->ratio;
							img->interlaced = this->interlaced;
							img->picture_number = procOut.PicInfo.picture_number;

              if(this->use_threading) {
  						//	transferbuff = NULL;

		  					xine_list_push_back(this->image_buffer, img);
              } else {
                crystalhd_video_render(this, img);
              }

						}
					}
          //if(!this->use_threading) {
            DtsReleaseOutputBuffs(hDevice, NULL, FALSE);
          //}
					break;
        case BC_STS_DEC_NOT_OPEN:
        case BC_STS_DEC_NOT_STARTED:
          break;
	   		default:
	      	if (ret > 26) {
		        	lprintf("DtsProcOutput returned %d.\n", ret);
		     	} else {
	  	   	  	lprintf("DtsProcOutput returned %s.\n", g_DtsStatusText[ret]);
					}
	       	break;
	   	}
		}

    if(this->use_threading) {
      //pthread_mutex_unlock(&this->rec_mutex);
      msleep(5);
    } else {
      break;
   }

	}

  //if(transferbuff) {
	//  _aligned_free(transferbuff);
	//  transferbuff = NULL;
  //}

  if(this->use_threading) {
	  pthread_exit(NULL);
  }

  return NULL;
}

static int vc1_find_header(crystalhd_video_decoder_t *this, buf_element_t *buf)
{
  uint8_t *p = buf->content;

  if (!p[0] && !p[1] && p[2] == 1 && p[3] == 0x0f) {
    int i;

    this->av_context->extradata = calloc(1, buf->size);
    this->av_context->extradata_size = 0;

    for (i = 0; i < buf->size && i < 128; i++) {
      if (!p[i] && !p[i+1] && p[i+2]) {
	      lprintf("00 00 01 %02x at %d\n", p[i+3], i);
	      if (p[i+3] != 0x0e && p[i+3] != 0x0f)
	        break;
      }
      this->av_context->extradata[i] = p[i];
      this->av_context->extradata_size++;
    }

    lprintf("ff_video_decoder: found VC1 sequence header\n");
    return 1;
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "crystalhd_video: VC1 extradata missing !\n");
  return 0;
}

static void crystalhd_init_video_decoder (crystalhd_video_decoder_t *this, buf_element_t *buf) {
  uint8_t *extradata = NULL;
  uint32_t extradata_size = 0;
  uint32_t startcode_size = 4;

  if(this->decoder_init)
    return;

  if(this->m_chd_params.sps_pps_buf)
   free(this->m_chd_params.sps_pps_buf);
  this->m_chd_params.sps_pps_buf = NULL;

  switch(buf->type) {
    case BUF_VIDEO_VC1:
      lprintf("BUF_VIDEO_VC1\n");
      this->algo = BC_MSUBTYPE_VC1;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    case BUF_VIDEO_WMV9:
      lprintf("BUF_VIDEO_WMV9\n");
      this->algo = BC_MSUBTYPE_WMV3;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    case BUF_VIDEO_H264:
      lprintf("BUF_VIDEO_H264\n");
      if ( this->av_context->extradata_size && *(char*)this->av_context->extradata == 1) {
        this->algo = BC_MSUBTYPE_AVC1;
        this->m_chd_params.sps_pps_buf = (uint8_t*)malloc(1000);
        if (!extract_sps_pps_from_avcc(this, this->av_context->extradata_size, this->av_context->extradata)) {
          free(this->m_chd_params.sps_pps_buf);
          this->m_chd_params.sps_pps_buf = NULL;
        } else {
          extradata = this->m_chd_params.sps_pps_buf;
          extradata_size = this->m_chd_params.sps_pps_size;
          startcode_size = this->m_chd_params.nal_size_bytes;
        }
      } else {
        this->algo = BC_MSUBTYPE_H264;
        extradata = this->av_context->extradata;
        extradata_size = this->av_context->extradata_size;
      }
      break;
    case BUF_VIDEO_MPEG:
      lprintf("BUF_VIDEO_MPEG\n");
      this->algo = BC_MSUBTYPE_MPEG2VIDEO;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    case BUF_VIDEO_MPEG4:
      lprintf("BUF_VIDEO_MPEG4\n");
      this->algo = BC_MSUBTYPE_DIVX;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    case BUF_VIDEO_XVID:
      lprintf("BUF_VIDEO_XVID\n");
      this->algo = BC_MSUBTYPE_DIVX;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    case BUF_VIDEO_DIVX5:
      lprintf("BUF_VIDEO_DIVX5\n");
      this->algo = BC_MSUBTYPE_DIVX;
      extradata = this->av_context->extradata;
      extradata_size = this->av_context->extradata_size;
      break;
    default:
      return;
  }

  this->stream_type = BC_STREAM_TYPE_ES;

  hDevice = crystalhd_start(this, hDevice, startcode_size, extradata, extradata_size, 
                              this->av_context->width, this->av_context->height, 
                              this->scaling_enable, this->scaling_width);
  this->decoder_init = 1;
}

static void crystalhd_init_video_codec (crystalhd_video_decoder_t *this, buf_element_t *buf) {

  switch(buf->type) {
    case BUF_VIDEO_VC1:
      this->codec_type = CODEC_ID_VC1;
      break;
    case BUF_VIDEO_WMV9:
      this->codec_type = CODEC_ID_WMV3;
      break;
    case BUF_VIDEO_H264:
      this->codec_type = CODEC_ID_H264;
      break;
    case BUF_VIDEO_MPEG:
      this->codec_type = CODEC_ID_MPEG2VIDEO;
      break;
    case BUF_VIDEO_MPEG4:
    case BUF_VIDEO_XVID:
    case BUF_VIDEO_DIVX5:
      this->codec_type = CODEC_ID_MPEG4;
      break;
    default:
      return;
  }

  if(this->decoder_init_mode) {
    this->av_codec = NULL;
    pthread_mutex_lock(&ffmpeg_lock);

    this->av_codec = avcodec_find_decoder(this->codec_type);
    if(!this->av_codec) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: error opening codec %d\n", this->codec_type);
      return;
    } 
    avcodec_open(this->av_context, this->av_codec);

    if (this->av_codec->id == CODEC_ID_VC1 &&
        (!this->bih.biWidth || !this->bih.biHeight)) {
      avcodec_close(this->av_context);

      if (avcodec_open (this->av_context, this->av_codec) < 0) {
        pthread_mutex_unlock(&ffmpeg_lock);
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
        return;
      }
    }

    this->av_parser = av_parser_init(this->codec_type);
    if(this->av_parser == NULL) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,"crystalhd: parser for codec %d not found\n", this->codec_type);
    } else {
      this->av_parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    }

    pthread_mutex_unlock(&ffmpeg_lock);
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "h264");

    this->decoder_init_mode = 0;
  }

  this->av_context->width = this->bih.biWidth;
  this->av_context->height = this->bih.biHeight;

  this->av_context->stream_codec_tag = this->av_context->codec_tag =
    _x_stream_info_get(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC);

  this->av_context->workaround_bugs = 1;
}
  
static int get_buffer_frame(AVCodecContext *av_context, AVFrame *av_frame)
{
  return avcodec_default_get_buffer( av_context, av_frame );
}

static void check_bufsize (crystalhd_video_decoder_t *this, int size) {
  if (size > this->bufsize) {
    this->bufsize = size + size / 2;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("crystalhd_video: increasing buffer to %d to avoid overflow.\n"),
	    this->bufsize);
    this->buf = realloc(this->buf, this->bufsize + FF_INPUT_BUFFER_PADDING_SIZE );
  }
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void crystalhd_video_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {
  AVRational avr00 = {0, 1};

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;
  uint8_t *chunk_buf = this->buf;

  //if(!this->pts && buf->pts)
  //  this->pts = buf->pts;

  if(buf->pts)
    this->pts = buf->pts;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) 
    return;

  if ( !buf->size )
    return;
  
  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    this->ratio = (double)buf->decoder_info[1]/(double)buf->decoder_info[2];
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (this->video_step != this->reported_video_step){
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
  }

  if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    //lprintf("BUF_FLAG_STDHEADER\n");
    memcpy ( &this->bih, buf->content, sizeof(xine_bmiheader) );

    if (this->bih.biSize > sizeof(xine_bmiheader)) {
      this->av_context->extradata_size = this->bih.biSize - sizeof(xine_bmiheader);
      this->av_context->extradata = malloc(this->av_context->extradata_size +
                                        FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(this->av_context->extradata, buf->content + sizeof(xine_bmiheader),
             this->av_context->extradata_size);
   }
   this->size = 0;
  } else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM &&
        !this->av_context->extradata_size) {

      //lprintf("BUF_SPECIAL_STSD_ATOM\n");
      this->av_context->extradata_size = buf->decoder_info[2];
      this->av_context->extradata = malloc(buf->decoder_info[2] +
  				      FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(this->av_context->extradata, buf->decoder_info_ptr[2],
        buf->decoder_info[2]);

    } else if (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG &&
              !this->av_context->extradata_size) {

      //lprintf("BUF_SPECIAL_DECODER_CONFIG\n");
      this->av_context->extradata_size = buf->decoder_info[2];
      this->av_context->extradata = malloc(buf->decoder_info[2] +
  				      FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(this->av_context->extradata, buf->decoder_info_ptr[2],
        buf->decoder_info[2]);
    }
  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {
    check_bufsize(this, this->size + buf->size);
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;
  } else {

    if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
      //lprintf("BUF_FLAG_FRAME_START\n");
      this->size = 0;
    }

    /* data accumulation */
    if (buf->size > 0) {
      if ((this->size == 0) &&
  	      ((buf->size + FF_INPUT_BUFFER_PADDING_SIZE) < buf->max_size) &&
  	      (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
        /* buf contains a complete frame */
        /* no memcpy needed */
        chunk_buf = buf->content;
        this->size = buf->size;
        //lprintf("no memcpy needed to accumulate data\n");
      } else {
        /* copy data into our internal buffer */
        check_bufsize(this, this->size + buf->size);
        chunk_buf = this->buf; /* check_bufsize might realloc this->buf */
  
        xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  
        this->size += buf->size;
        //lprintf("accumulate data into this->buf\n");
      }
    }
  
    //if (buf->decoder_flags & BUF_FLAG_FRAME_END)
    //    lprintf("BUF_FLAG_FRAME_END\n");

    if ((buf->decoder_flags & BUF_FLAG_FRAME_END) || (buf->type == BUF_VIDEO_MPEG)) { // ||
        //(buf->type == BUF_VIDEO_VC1) || (buf->type == BUF_VIDEO_WMV9)) {

      int         len;
      int         offset = 0;
      int         video_step_to_use = this->video_step;

      if(this->decoder_init_mode) {
        crystalhd_init_video_codec(this, buf);
      }

      int decode = 0;

      if(buf->type == BUF_VIDEO_VC1) {
        decode = vc1_find_header(this, buf);
      } else {
        decode = 1;
      }

      while (this->size > 0 && !this->decoder_init_mode && decode) {

        uint8_t *poutbuf;
        int poutbuf_size = 0;

        memset(&chunk_buf[this->size], 0, FF_INPUT_BUFFER_PADDING_SIZE);

        /* Decode first frame in software */
        if(!this->av_got_picture) {
          AVPacket         av_pkt;

          av_init_packet(&av_pkt);
          av_pkt.size = this->size;
          av_pkt.data = &chunk_buf[offset];

          len = avcodec_decode_video2 (this->av_context, this->av_frame, &this->av_got_picture, &av_pkt);

          /*
          if(this->av_parser) {
            av_parser_parse2(this->av_parser, this->av_context, &poutbuf, &poutbuf_size,
                               &chunk_buf[offset], this->size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
          } else {
            len = this->size;
            poutbuf = &chunk_buf[offset];
            poutbuf_size = this->size;
          }
          
          if(this->av_got_picture) {
            lprintf("got first decoded picture size %d %lld\n", len, this->av_frame->pts);

            AVFrame *frame = this->av_context->coded_frame;
            
          }
          */

          /* use externally provided video_step or fall back to stream's time_base otherwise */
          video_step_to_use = (this->video_step || !this->av_context->time_base.den)
                            ? this->video_step
                            : (int)(90000ll
                                    * this->av_context->ticks_per_frame
                                    * this->av_context->time_base.num / this->av_context->time_base.den);

          this->video_step = video_step_to_use;

          /* aspect ratio provided by ffmpeg, override previous setting */
          if ((this->aspect_ratio_prio < 2) &&
            av_cmp_q(this->av_context->sample_aspect_ratio, avr00)) {

            if (!this->bih.biWidth || !this->bih.biHeight) {
              this->bih.biWidth  = this->av_context->width;
              this->bih.biHeight = this->av_context->height;
            }

    	      this->ratio = av_q2d(this->av_context->sample_aspect_ratio) *
    	                                 (double)this->bih.biWidth / (double)this->bih.biHeight;
    	      this->aspect_ratio_prio = 2;
    	      lprintf("ffmpeg aspect ratio: %f %d\n", this->ratio, video_step_to_use);

          }
        } else { 
          if(this->av_parser) {
            len = av_parser_parse2(this->av_parser, this->av_context, &poutbuf, &poutbuf_size,
                               &chunk_buf[offset], this->size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
          } else {
            len = this->size;
            poutbuf = &chunk_buf[offset];
            poutbuf_size = this->size;
          }
        }

        //if(poutbuf_size > 0 || this->av_got_picture) {
        if(poutbuf_size > 0 || this->av_got_picture) {

          if(!this->decoder_init_mode) {
            crystalhd_init_video_decoder (this, buf);
          }
         
          /*
          uint8_t *psendbuf = NULL;
          int psendbuf_size = 0;

          if ( this->av_context->extradata_size && *(char*)this->av_context->extradata == 1 && !this->bitstream_convert) {

            bitstream_convert_init(this, this->av_context->extradata, this->av_context->extradata_size);
            this->bitstream_convert = 1;

          }
          */

          /*
          if(this->bitstream_convert) {
            bitstream_convert(this, poutbuf, poutbuf_size, &psendbuf, &psendbuf_size);

            if(psendbuf && (psendbuf_size > 0)) {
              crystalhd_send_data(this, hDevice, psendbuf, psendbuf_size, this->pts);
              free(psendbuf);
            }
          } else {
            crystalhd_send_data(this, hDevice, poutbuf, poutbuf_size, this->pts);
          }
          */

          //if(this->use_threading)
          //  pthread_mutex_lock(&this->rec_mutex);

          crystalhd_send_data(this, hDevice, poutbuf, poutbuf_size, this->pts);

          //if(this->use_threading)
          //  pthread_mutex_unlock(&this->rec_mutex);

        }

        if(this->use_threading) {
          crystalhd_video_render(this, NULL);
        }

        if(!this->use_threading) {
          crystalhd_video_rec_thread(this);
        }

        if ((len <= 0) || (len > this->size)) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "crystalhd_video: error parsing frame\n");
          this->size = 0;
        } else {
  
          offset += len;
          this->size -= len;
  
          if (this->size > 0) {
            check_bufsize(this, this->size);
            memmove (this->buf, &chunk_buf[offset], this->size);
            chunk_buf = this->buf;
          }
        }
      }
    }
  }
}

static void crystalhd_video_clear_all_pts(crystalhd_video_decoder_t *this) {

	xine_list_iterator_t ite;
	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
    img->pts = 0;
	}

	if(hDevice) {
		DtsFlushInput(hDevice, 4);
	}

}

static void crystalhd_video_clear_worker_buffers(crystalhd_video_decoder_t *this) {
	xine_list_iterator_t ite;

  //lprintf("crystalhd_video_clear_worker_buffers enter\n");

	if(hDevice) {
		DtsFlushInput(hDevice, 4);
	}

	while ((ite = xine_list_front(this->image_buffer)) != NULL) {
		image_buffer_t	*img = xine_list_get_value(this->image_buffer, ite);
		free(img->image);
		free(img);
		xine_list_remove(this->image_buffer, ite);
	}

  //lprintf("crystalhd_video_clear_worker_buffers leave\n");
}

static void crystalhd_video_destroy_workers(crystalhd_video_decoder_t *this) {

  if(this->use_threading) {
  	if(this->rec_thread) {
	  	this->rec_thread_stop = 1;
	  }
	  pthread_mutex_destroy(&this->rec_mutex);
  }

}

static void crystalhd_video_setup_workers(crystalhd_video_decoder_t *this) {
				
  if(this->use_threading) {
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&this->rec_thread, &thread_attr,crystalhd_video_rec_thread,(void *)this);
    pthread_attr_destroy(&thread_attr);

	  pthread_mutex_init(&this->rec_mutex, NULL);
  }

}

/*
 * This function is called when xine needs to flush the system.
 */
static void crystalhd_video_flush (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t*) this_gen;

	crystalhd_video_clear_worker_buffers(this);

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_decode_flush\n");
}

/*
 * This function resets the video decoder.
 */
static void crystalhd_video_reset (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  this->last_image        = 0;
  this->av_got_picture    = 0;
  this->size              = 0;
  this->pts               = 0;

	crystalhd_video_clear_worker_buffers(this);

  this->decoder_init           = 0;
  //this->decoder_init_mode      = 1;

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_reset\n");
}

/*
 * The decoder should forget any stored pts values here.
 */
static void crystalhd_video_discontinuity (video_decoder_t *this_gen) {
  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

  this->pts               = 0;

  crystalhd_video_clear_all_pts(this);

  this->reset = VO_NEW_SEQUENCE_FLAG;

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_discontinuity\n");
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void crystalhd_video_dispose (video_decoder_t *this_gen) {

  crystalhd_video_decoder_t *this = (crystalhd_video_decoder_t *) this_gen;

	crystalhd_video_destroy_workers(this);

	hDevice = crystalhd_stop(this, hDevice);

	crystalhd_video_clear_worker_buffers(this);
  xine_list_delete(this->image_buffer);

  this->decoder_init      = 0;

  pthread_mutex_lock(&ffmpeg_lock);
  avcodec_close (this->av_context);
  pthread_mutex_unlock(&ffmpeg_lock);

  if(this->av_context && this->av_context->slice_offset)
    free(this->av_context->slice_offset);

  if(this->av_context && this->av_context->extradata)
    free(this->av_context->extradata);

  if( this->av_context )
    av_free( this->av_context );

  if( this->av_frame )
    av_free( this->av_frame );

  if(this->av_parser)
  {
    av_parser_close(this->av_parser);
    av_free( this->av_parser );
  }

  if (this->buf)
    free(this->buf);
  this->buf = NULL;

  if (this->m_sps_pps_context.sps_pps_data)
    free(this->m_sps_pps_context.sps_pps_data);
  this->m_sps_pps_context.sps_pps_data = NULL;
    
  if (this->m_chd_params.sps_pps_buf)
    free(this->m_chd_params.sps_pps_buf);
  this->m_chd_params.sps_pps_buf = NULL;
    
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: crystalhd_video_dispose\n");
  free (this);
}

void crystalhd_scaling_enable( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->scaling_enable = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_enable %d\n", this->scaling_enable);
}

void crystalhd_scaling_width( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->scaling_width = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_width %d\n", this->scaling_width);
}

void crystalhd_use_threading( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->use_threading = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: use_threading %d\n", this->use_threading);
}

void crystalhd_extra_logging( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->extra_logging = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: extra_logging %d\n", this->extra_logging);
}

void crystalhd_decoder_reopen( void *this_gen, xine_cfg_entry_t *entry )
{
  crystalhd_video_decoder_t  *this  = (crystalhd_video_decoder_t *) this_gen;

  this->decoder_reopen = entry->num_value;
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_reopen %d\n", this->decoder_reopen);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *crystalhd_video_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  crystalhd_video_decoder_t  *this ;
  config_values_t *config;
  
  this = (crystalhd_video_decoder_t *) calloc(1, sizeof(crystalhd_video_decoder_t));

  this->video_decoder.decode_data         = crystalhd_video_decode_data;
  this->video_decoder.flush               = crystalhd_video_flush;
  this->video_decoder.reset               = crystalhd_video_reset;
  this->video_decoder.discontinuity       = crystalhd_video_discontinuity;
  this->video_decoder.dispose             = crystalhd_video_dispose;

  this->stream                            = stream;
  this->xine                              = stream->xine;
  this->class                             = (crystalhd_video_class_t *) class_gen;

  config                                  = this->xine->config;

  this->scaling_enable = config->register_bool( config, "video.crystalhd_decoder.scaling_enable", 0,
    _("crystalhd_video: enable decoder scaling"),
    _("Set to true if you want to enable scaling.\n"),
    10, crystalhd_scaling_enable, this );

  this->scaling_width = config->register_num( config, "video.crystalhd_decoder.scaling_width", 0,
    _("crystalhd_video: scaling width"),
    _("Set it to the scaled width.\n"),
    1920, crystalhd_scaling_width, this );

  this->use_threading = config->register_bool( config, "video.crystalhd_decoder.use_threading", 1,
    _("crystalhd_video: use threading"),
    _("Set this to false if you wanna have no recieve thread.\n"),
    10, crystalhd_use_threading, this );

  this->extra_logging = config->register_bool( config, "video.crystalhd_decoder.extra_logging", 0,
    _("crystalhd_video: enable extra logging"),
    _("Set this to true if you wanna have extra logging.\n"),
    10, crystalhd_extra_logging, this );

  this->decoder_reopen = config->register_bool( config, "video.crystalhd_decoder.decoder_reopen", 0,
    _("crystalhd_video: use full decoder reopen."),
    _("due a bug in bcm70015 set this to true for bcm70015.\n"),
    10, crystalhd_decoder_reopen, this );

	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_enable %d\n", this->scaling_enable);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: scaling_width  %d\n", this->scaling_width);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: use_threading  %d\n", this->use_threading);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: extra_logging  %d\n", this->extra_logging);
	xprintf(this->xine, XINE_VERBOSITY_LOG, "crystalhd_video: decoder_reopen %d\n", this->decoder_reopen);

  this->video_step  	    = 0;
  this->reported_video_step = 0;
  this->ratio  				    = 0;

	this->width							= 1920;
	this->height						= 1082;
	this->y_size						= this->width * this->height * 2;
	this->uv_size						= 0;

	this->interlaced        = 0;
	this->last_image				= 0;

	this->rec_thread_stop 	= 0;

	this->image_buffer      = xine_list_new();

  this->reset             = VO_NEW_SEQUENCE_FLAG;

	crystalhd_video_setup_workers(this);

  this->av_context        = avcodec_alloc_context2(CODEC_TYPE_VIDEO);
  this->av_context->get_buffer = get_buffer_frame;
  this->av_frame          = avcodec_alloc_frame();
  this->av_codec          = NULL;
  this->av_parser         = NULL;
  this->decoder_init_mode = 1;
  this->decoder_init      = 0;
  this->av_got_picture    = 0;
  this->buf               = calloc(1, VIDEOBUFSIZE + FF_INPUT_BUFFER_PADDING_SIZE);
  this->bufsize           = VIDEOBUFSIZE;
  this->size              = 0;
  this->aspect_ratio_prio = 1;
  this->bitstream_convert = 0;
  this->m_sps_pps_context.sps_pps_data = NULL;
  this->codec_type        = 0;
  this->pts               = 0;

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *init_video_plugin (xine_t *xine, void *data) {

  crystalhd_video_class_t *this;
  int use_threading;

  this = (crystalhd_video_class_t *) calloc(1, sizeof(crystalhd_video_class_t));

  this->decoder_class.open_plugin     = crystalhd_video_open_plugin;
  this->decoder_class.identifier      = "crystalhd_video";
  this->decoder_class.description     =
	N_("crystalhd_video: Video decoder plugin using CrystalHD hardware decoding.");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  use_threading = xine->config->register_bool( xine->config, "video.crystalhd_decoder.use_threading", 1,
    _("crystalhd_video: use threading"),
    _("Set this to false if you wanna have no recieve thread.\n"),
    10, crystalhd_use_threading, this );

  hDevice = crystalhd_open(use_threading);

  pthread_once( &once_control, init_once_routine );

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
uint32_t video_types[] = {
  BUF_VIDEO_H264, 
  BUF_VIDEO_VC1, 
  BUF_VIDEO_WMV9, 
  BUF_VIDEO_MPEG, 
  BUF_VIDEO_MPEG4,
  BUF_VIDEO_XVID,
  BUF_VIDEO_DIVX5,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
decoder_info_t dec_info_crystalhd_video = {
  video_types,     /* supported types */
  7                /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "crystalhd_decoder", XINE_VERSION_CODE, &dec_info_crystalhd_video, init_video_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

