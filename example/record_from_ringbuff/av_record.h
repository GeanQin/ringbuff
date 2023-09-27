#ifndef __AV_RECORD_H__
#define __AV_RECORD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "stdint.h"
#include "merr.h"

#define V_BUFF_SIZE_MAX 307200
#define A_BUFF_SIZE_MAX 4096
#define SDCARD_PATH "/media/mmcblk0p1/video_record"
#define SDCARD_MP4_INDEX_PATH "/media/mmcblk0p1/video_record/.index"
#define MP4_DURATION 60 // 60s
#define ONE_DIR_DURATION (60 * 60) // 1h

typedef struct
{
    uint32_t dir_ts;
    uint32_t file_ts;
    uint8_t duration;
    uint8_t event_flag;
    uint8_t keep_flag;
    uint8_t deleted : 1;
} record_index_t;

M_ERR av_record_mp4_by_ringbuff_start();
void av_record_mp4_by_ringbuff_stop();

#ifdef __cplusplus
}
#endif

#endif