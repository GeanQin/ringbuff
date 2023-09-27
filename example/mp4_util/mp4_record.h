#ifndef __MP4_UTIL_H__
#define __MP4_UTIL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define USE_BOARD_AV 1
#ifdef USE_BOARD_AV
#include <board_av.h>
#else
typedef enum  
{
	FRAME_TYPE_VIDEO,
	FRAME_TYPE_AUDIO
} frame_type_e;

typedef enum  
{
	VENC_FORMAT_H264,   // H.264
	VENC_FORMAT_H265,   // H.265
	VENC_FORMAT_JPEG    // JPEG
} venc_format_e;

typedef enum  
{
	AENC_FORMAT_NONE = 0,
	AENC_FORMAT_PCM = AENC_FORMAT_NONE,
	AENC_FORMAT_AAC = 1,
	AENC_FORMAT_G711A,
	AENC_FORMAT_OPUS
} aenc_format_e;

typedef struct 
{
	int chn;
	unsigned char *data;            /* 指向视频码流数据。*/
	int data_len;                   /* 视频码流数据长度。*/
	frame_type_e type;              /* 视频码流对应的帧类型（指的是视频，音频，数据等）。*/
	int key_frame;                  /* 是否关键帧 */
	venc_format_e venc_type;        /* 视频码流对应的编码类型（指的是H264，H265等）。*/
	aenc_format_e aenc_type;
	int width;                      /* 视频图像宽。*/
	int height;                     /* 视频图像高。*/
    int seq;
	unsigned long long ts;          /* 时间戳。*/
} frame_info_t;
#endif

int ffmpeg_create_mp4(char *file_name, frame_info_t *frame, aenc_format_e audio_format);
int ffmpeg_write_mp4(frame_info_t *frame);
void ffmpeg_stop_mp4();

#ifdef __cplusplus
}
#endif

#endif