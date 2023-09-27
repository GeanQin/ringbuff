#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libavformat/avformat.h"
#include "libavutil/mathematics.h"

#include "merr.h"
#include "mlog.h"
#include "board_av.h"
#include "ringbuff.h"
#include "systime.h"
#include "mp4_record.h"
#include "av_record.h"

#define TMP_MP4_FILE "/media/mmcblk0p1/tmp.mp4"

static uint8_t record_start_flag = 0;

static void add_to_index_file(record_index_t *index_item)
{
    FILE *fp = NULL;

    fp = fopen(SDCARD_MP4_INDEX_PATH, "a+");
    if (fp != NULL)
    {
        fwrite(index_item, sizeof(record_index_t), 1, fp);
        fclose(fp);
    }
    else
    {
        mlog_err("[%s] can not open index file", __func__);
    }
}

static void *av_record(void *arg)
{
    ringbuff_fd v_fd = -1;
    ringbuff_fd a_fd = -1;
    ringbuff_read_fd v_read_fd = -1;
    ringbuff_read_fd a_read_fd = -1;
    u_int8_t *v_data = NULL;
    int64_t v_get_len = 0;
    u_int8_t *a_data = NULL;
    int64_t a_get_len = 0;
    u_int8_t one_mp4_start = 0;
    u_int8_t v_have_data_flag = 0;
    u_int8_t a_have_data_flag = 0;
    frame_info_t *v_frame = NULL;
    frame_info_t *a_frame = NULL;
    unsigned long now_ts = 0;
    unsigned long start_ts = 0;
    unsigned long dir_ts = 0;
    char dir_path[128] = {0};
    char file_name[128] = {0};
    record_index_t index_item;

    pthread_detach(pthread_self());

    v_data = (u_int8_t *)malloc(V_BUFF_SIZE_MAX);
    a_data = (u_int8_t *)malloc(A_BUFF_SIZE_MAX);

    if (access(SDCARD_PATH, F_OK) != 0)
    {
        mlog_info("%s is not exist,create it", SDCARD_PATH);
        mkdir(SDCARD_PATH, 0777);
    }

    // 等时间同步，1693497600=2023-09-1 00:00:00
    while (dir_ts = systime_get_ts() < 1693497600)
    {
        sleep(1);
    }
    
    dir_ts = systime_get_ts();
    memset(dir_path, 0, sizeof(dir_path));
    snprintf(dir_path, sizeof(dir_path) - 1, "%s/%lu", SDCARD_PATH, dir_ts);
    mkdir(dir_path, 0777);

    while (1)   // 等ringbuff初始化了
    {
        v_fd = ringbuff_get_fd(VI_RINGBUFF_NAME);
        a_fd = ringbuff_get_fd(AI_RINGBUFF_NAME);
        if (v_fd < 0 || a_fd < 0)
        {
            mlog_err("[%s] ringbuff_get_fd err", __func__);
            sleep(1);
            continue;
        }

        sleep(3);

        // 获取读的描述符
        v_read_fd = ringbuff_read_init(v_fd, 0);
        if (v_read_fd < 0)
        {
            mlog_err("[%s] ringbuff_read_init v_read_fd err", __func__);
            continue;
        }
        a_read_fd = ringbuff_read_init(a_fd, 0);
        if (a_read_fd < 0)
        {
            mlog_err("[%s] ringbuff_read_init a_read_fd err", __func__);
            ringbuff_read_deinit(v_read_fd);
            continue;
        }
        mlog_debug("[%s] v_read_fd=%d, a_read_fd=%d", __func__, v_read_fd, a_read_fd);

        break;
    }

    while (record_start_flag == 1)
    {
        v_get_len = ringbuff_read(v_read_fd, v_data, V_BUFF_SIZE_MAX);
        if (v_get_len > 0 && v_get_len < V_BUFF_SIZE_MAX)
        {
            v_have_data_flag = 1;
            v_frame = (frame_info_t *)v_data;
            v_frame->data = v_data + sizeof(frame_info_t);
        }

        a_get_len = ringbuff_read(a_read_fd, a_data, A_BUFF_SIZE_MAX);
        if (a_get_len > 0 && a_get_len < A_BUFF_SIZE_MAX)
        {
            a_have_data_flag = 1;
            a_frame = (frame_info_t *)a_data;
            a_frame->data = a_data + sizeof(frame_info_t);
        }

        if (v_have_data_flag == 1)
        {
            // mlog_debug("[%s]one_mp4_start=%d, video ts=%llu", __func__, one_mp4_start, v_frame->ts);
            if (one_mp4_start == 0 && v_frame->key_frame == 1)
            {
                start_ts = systime_get_ts();
                memset(file_name, 0, sizeof(file_name));
                snprintf(file_name, sizeof(file_name) - 1, "%s/%lu.mp4", dir_path, start_ts);

                remove(TMP_MP4_FILE);
                if (ffmpeg_create_mp4(TMP_MP4_FILE, v_frame, AENC_FORMAT_AAC) < 0)
                {
                    mlog_err("ffmpeg_create_mp4 err");
                }
                else
                {
                    mlog_info("[%s]%s start write", __func__, file_name);
                    one_mp4_start = 1;
                    memset(&index_item, 0, sizeof(record_index_t));
                    index_item.dir_ts = dir_ts;
                    index_item.file_ts = start_ts;
                    index_item.event_flag = 0;
                    index_item.keep_flag = 0;
                    index_item.duration = MP4_DURATION;
                    index_item.deleted = 0;
                }
            }

            if (one_mp4_start == 1)
            {
                ffmpeg_write_mp4(v_frame);
                v_have_data_flag = 0;
            }
        }

        if (a_have_data_flag == 1)
        {
            // mlog_debug("[%s]one_mp4_start=%d, audio ts=%llu", __func__, one_mp4_start, a_frame->ts);
            if (one_mp4_start == 1)
            {
                ffmpeg_write_mp4(a_frame);
                a_have_data_flag = 0;
            }
        }

        if (one_mp4_start == 1)
        {
            now_ts = systime_get_ts();
            if (now_ts - start_ts >= MP4_DURATION)
            {
                ffmpeg_stop_mp4();
                rename(TMP_MP4_FILE, file_name);
                add_to_index_file(&index_item);
                one_mp4_start = 0;
                mlog_info("[%s] %s write complete", __func__, file_name);
            }
            if (now_ts - dir_ts >= ONE_DIR_DURATION)
            {
                dir_ts = systime_get_ts();
                memset(dir_path, 0, sizeof(dir_path));
                snprintf(dir_path, sizeof(dir_path) - 1, "%s/%lu", SDCARD_PATH, dir_ts);
                mkdir(dir_path, 0777);
            }
        }
    }

    if (v_data != NULL)
    {
        free(v_data);
    }

    if (a_data != NULL)
    {
        free(a_data);
    }
}

M_ERR av_record_mp4_by_ringbuff_start()
{
    int ret = 0;
    pthread_t pid;

    record_start_flag = 1;

    ret = pthread_create(&pid, 0, av_record, NULL);
    if (ret != 0)
    {
        mlog_err("[%s] av_record start err", __func__);
        return M_RECORD_START_ERR;
    }

    mlog_info("[%s] av record start", __func__);

    return M_NONE_ERR;
}

void av_record_mp4_by_ringbuff_stop()
{
    record_start_flag = 0;
    mlog_info("[%s] av record stop", __func__);
}