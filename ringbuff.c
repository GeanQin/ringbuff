#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "ringbuff.h"

static ringbuff_t ringbuff[RINGBUFF_COUNT_MAX];
static ringbuff_client_t ringbuff_client[RINGBUFF_CLIENT_COUNT_MAX];

static int8_t get_item_info(int8_t to_before, ringbuff_item_info_t *item_info, uint8_t *read_p, ringbuff_fd fd)
{
    int i = 0;
    int64_t remain_len = 0;
    u_int8_t *ringbuff_end_p = NULL;
    u_int8_t *item_head = (u_int8_t *)item_info;
    static u_int64_t pre_read_seq = 0;

    ringbuff_end_p = ringbuff[fd].ringbuff_p + ringbuff[fd].ringbuff_size;
    remain_len = ringbuff_end_p - read_p;
    if (remain_len <= sizeof(ringbuff_item_info_t))
    {
        // printf("[%s]splice item_info! read_p=%p remain_len=%ld\n", __func__, read_p, remain_len);
        memcpy(item_head, read_p, remain_len);
        memcpy(item_head + remain_len, ringbuff[fd].ringbuff_p, sizeof(ringbuff_item_info_t) - remain_len);
    }
    else
    {
        memcpy(item_info, read_p, sizeof(ringbuff_item_info_t));
    }

    if (item_info->data_info_p > ringbuff_end_p || ringbuff_end_p < ringbuff[fd].ringbuff_p)
    {
        return -1;
    }

    if (item_info->data_size < 0 || item_info->data_size + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        return -1;
    }

    if (item_info->pre_data_size < 0 || item_info->pre_data_size + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        return -1;
    }

    for (i = 0; i < sizeof(item_info->separator); i++)
    {
        if (item_info->separator[i] != 1)
        {
            return -1;
        }
    }

    // 防止读到之前的数据了
    if (to_before == 0)
    {
        // printf("pre_seq=%lu, now_seq=%lu\n", pre_read_seq, item_info->seq);
        if (item_info->seq < pre_read_seq)
        {
            return -1;   
        }
        pre_read_seq = item_info->seq;
    }

    printf("[%s]data_info_p=%p, data_size=%lu, pre_data_size=%lu, separator=[%d,%d], seq=%lu\n", __func__,
           item_info->data_info_p, item_info->data_size, item_info->pre_data_size, 
           item_info->separator[0], item_info->separator[7], item_info->seq);

    return 0;
}

ringbuff_fd ringbuff_init(char *ringbuff_tag, int64_t size)
{
    int i = 0;
    int use_index = -1;

    for (i = 0; i < RINGBUFF_COUNT_MAX; i++)
    {
        if (ringbuff[i].have_init == 0)
        {
            use_index = i;
            break;
        }
    }

    if (use_index < 0)
    {
        return RINGBUFF_ALL_IN_USE;
    }

    memset(&ringbuff[use_index], 0, sizeof(ringbuff_t));
    memcpy(ringbuff[use_index].ringbuff_tag, ringbuff_tag, strlen(ringbuff_tag) + 1);
    ringbuff[use_index].ringbuff_p = (u_int8_t *)malloc(size);
    if (ringbuff[use_index].ringbuff_p == NULL)
    {
        return RINGBUFF_MALLOC_ERR;
    }
    ringbuff[use_index].ringbuff_size = size;
    ringbuff[use_index].last_item = (ringbuff_item_info_t *)malloc(sizeof(ringbuff_item_info_t));
    if (ringbuff[use_index].last_item == NULL)
    {
        return RINGBUFF_MALLOC_ERR;
    }
    ringbuff[use_index].last_item->data_info_p = NULL;
    ringbuff[use_index].last_item->data_size = 0;
    ringbuff[use_index].last_item->pre_data_size = 0;
    ringbuff[use_index].last_item->seq = 0;
    memset(ringbuff[use_index].last_item->separator, 1, sizeof(ringbuff[use_index].last_item->separator));
    ringbuff[use_index].write_p = ringbuff[use_index].ringbuff_p;
    pthread_mutex_init(&(ringbuff[use_index].ringbuff_lock), NULL);
    ringbuff[use_index].have_init = 1;

    return use_index;
}

ringbuff_fd ringbuff_get_fd(char *ringbuff_tag)
{
    int i = 0;
    int index = -1;

    for (i = 0; i < RINGBUFF_COUNT_MAX; i++)
    {
        if (ringbuff[index].have_init == 1 &&
            strncmp(ringbuff_tag, ringbuff[index].ringbuff_tag, strlen(ringbuff_tag) + 1) == 0)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        return RINGBUFF_CANNOT_FIND;
    }

    return index;
}

void ringbuff_reset(ringbuff_fd fd)
{
    pthread_mutex_lock(&(ringbuff[fd].ringbuff_lock));
    if (ringbuff[fd].have_init == 1)
    {
        ringbuff[fd].write_p = ringbuff[fd].ringbuff_p;
        ringbuff[fd].last_item->data_info_p = NULL;
        ringbuff[fd].last_item->data_size = 0;
        ringbuff[fd].last_item->pre_data_size = 0;
        ringbuff[fd].last_item->seq = 0;
        memset(ringbuff[fd].last_item->separator, 1, sizeof(ringbuff[fd].last_item->separator));
    }
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));
}

int64_t ringbuff_put(ringbuff_fd fd, u_int8_t *data, int64_t data_len)
{
    u_int8_t *ringbuff_end_p = NULL;
    u_int8_t *item_head = NULL;
    int64_t remain_size = 0;
    int64_t write_len = 0;
    static u_int64_t write_seq = 0;

    if (ringbuff[fd].have_init == 0)
    {
        return RINGBUFF_DO_NOT_INIT;
    }

    if (data_len + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        return RINGBUFF_INPUT_DATA_ERR;
    }

    ringbuff_end_p = ringbuff[fd].ringbuff_p + ringbuff[fd].ringbuff_size;
    pthread_mutex_lock(&(ringbuff[fd].ringbuff_lock));
    if (ringbuff[fd].write_p < ringbuff[fd].ringbuff_p ||
        ringbuff[fd].write_p > ringbuff_end_p)
    {
        pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));
        fprintf(stderr, "[%s]ringbuff %s write_p is err, reset this ringbuff.\n",
                __func__, ringbuff[fd].ringbuff_tag);
        ringbuff_reset(fd);
        return RINGBUFF_WRITE_P_ERR;
    }

    ringbuff[fd].last_item->pre_data_size = ringbuff[fd].last_item->data_size;
    ringbuff[fd].last_item->data_size = data_len;
    ringbuff[fd].last_item->data_info_p = ringbuff[fd].write_p;
    ringbuff[fd].last_item->seq = write_seq;
    memset(ringbuff[fd].last_item->separator, 1, sizeof(ringbuff[fd].last_item->separator));
    item_head = (u_int8_t *)ringbuff[fd].last_item;

    // 到结尾不够，就尾部存部分，其他存开头
    if (ringbuff[fd].write_p + sizeof(ringbuff_item_info_t) >= ringbuff_end_p)
    {
        remain_size = ringbuff_end_p - ringbuff[fd].write_p;
        memcpy(ringbuff[fd].write_p, item_head, remain_size);
        memcpy(ringbuff[fd].ringbuff_p, item_head + remain_size, sizeof(ringbuff_item_info_t) - remain_size);
        ringbuff[fd].write_p = ringbuff[fd].ringbuff_p + sizeof(ringbuff_item_info_t) - remain_size;
    }
    else
    {
        memcpy(ringbuff[fd].write_p, ringbuff[fd].last_item, sizeof(ringbuff_item_info_t));
        ringbuff[fd].write_p += sizeof(ringbuff_item_info_t);
    }

    if (ringbuff[fd].write_p + data_len >= ringbuff_end_p)
    {
        remain_size = ringbuff_end_p - ringbuff[fd].write_p;
        memcpy(ringbuff[fd].write_p, data, remain_size);
        memcpy(ringbuff[fd].ringbuff_p, data + remain_size, data_len - remain_size);
        ringbuff[fd].write_p = ringbuff[fd].ringbuff_p + data_len - remain_size;
    }
    else
    {
        memcpy(ringbuff[fd].write_p, data, data_len);
        ringbuff[fd].write_p += data_len;
    }

    // 套圈
    if (ringbuff[fd].last_item->data_info_p >= ringbuff[fd].write_p)
    {
        write_len = ringbuff[fd].write_p - ringbuff[fd].ringbuff_p + ringbuff_end_p - ringbuff[fd].last_item->data_info_p;
    }
    else
    {
        write_len = ringbuff[fd].write_p - ringbuff[fd].last_item->data_info_p;
    }
    write_seq++;
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));

    return write_len;
}

void ringbuff_dump(ringbuff_fd fd)
{
    char filename[64] = {0};
    FILE *fp = NULL;

    snprintf(filename, sizeof(filename), "ringbuff-%s-offset%lu",
             ringbuff[fd].ringbuff_tag, ringbuff[fd].write_p - ringbuff[fd].ringbuff_p);

    fp = fopen(filename, "w");
    if (fp == NULL)
    {
        fprintf(stderr, "[%s]Cannot open %s\n", __func__, filename);
        return;
    }

    pthread_mutex_lock(&(ringbuff[fd].ringbuff_lock));
    fwrite(ringbuff[fd].ringbuff_p, 1, ringbuff[fd].ringbuff_size, fp);
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));

    fclose(fp);
}

ringbuff_read_fd ringbuff_read_init(ringbuff_fd fd, u_int32_t forward)
{
    ringbuff_err_e err = RINGBUFF_NONE_ERR;
    int i = 0;
    int use_index = -1;
    u_int8_t *ringbuff_end_p = NULL;
    ringbuff_item_info_t item_info;
    int64_t data_len = 0;
    int64_t before_len = 0;

    for (i = 0; i < RINGBUFF_CLIENT_COUNT_MAX; i++)
    {
        if (ringbuff_client[i].in_use == 0)
        {
            use_index = i;
            break;
        }
    }

    if (use_index < 0)
    {
        return RINGBUFF_CLIENT_ALL_IN_USE;
    }

    memset(&ringbuff_client[use_index], 0, sizeof(ringbuff_client_t));
    ringbuff_client[use_index].rb_fd = fd;

    pthread_mutex_lock(&(ringbuff[fd].ringbuff_lock));
    if (ringbuff[fd].have_init == 0)
    {
        err = RINGBUFF_DO_NOT_INIT;
        goto END;
    }

    ringbuff_client[use_index].read_p = ringbuff[fd].last_item->data_info_p;
    ringbuff_end_p = ringbuff[fd].ringbuff_p + ringbuff[fd].ringbuff_size;

    if (get_item_info(1, &item_info, ringbuff_client[use_index].read_p, fd) < 0)
    {
        err = RINGBUFF_READ_P_ERR;
        goto END;
    }
    // 判断已经是头了，不能往前偏移了
    if (item_info.pre_data_size == 0)
    {
        printf("read_p at head of ringbuff\n");
        goto END;
    }

    for (i = 0; i < forward; i++)
    {
        before_len = ringbuff_client[use_index].read_p - ringbuff[fd].ringbuff_p;
        if (before_len <= item_info.pre_data_size)
        {
            ringbuff_client[use_index].read_p = ringbuff_end_p - item_info.pre_data_size + before_len;
        }
        else
        {
            ringbuff_client[use_index].read_p -= item_info.pre_data_size;
        }

        before_len = ringbuff_client[use_index].read_p - ringbuff[fd].ringbuff_p;
        if (before_len <= sizeof(ringbuff_item_info_t))
        {
            ringbuff_client[use_index].read_p = ringbuff_end_p - sizeof(ringbuff_item_info_t) + before_len;
        }
        else
        {
            ringbuff_client[use_index].read_p -= sizeof(ringbuff_item_info_t);
        }
        if (get_item_info(1, &item_info, ringbuff_client[use_index].read_p, fd) < 0)
        {
            err = RINGBUFF_READ_P_ERR;
            goto END;
        }
    }

END:
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));
    if (err < 0)
    {
        return err;
    }

    ringbuff_client[use_index].in_use = 1;
    return use_index;
}

int64_t ringbuff_read(ringbuff_read_fd fd, u_int8_t *buf, int64_t buf_size)
{
    ringbuff_err_e err = RINGBUFF_NONE_ERR;
    ringbuff_item_info_t item_info;
    int64_t remain_size = 0;
    u_int8_t *ringbuff_end_p = NULL;
    ringbuff_fd rb_fd = 0;
    int64_t read_len = 0;

    rb_fd = ringbuff_client[fd].rb_fd;
    ringbuff_end_p = ringbuff[rb_fd].ringbuff_p + ringbuff[rb_fd].ringbuff_size;

    pthread_mutex_lock(&(ringbuff[rb_fd].ringbuff_lock));
    if (ringbuff[rb_fd].have_init == 0)
    {
        err = RINGBUFF_DO_NOT_INIT;
        goto END;
    }

    if (get_item_info(0, &item_info, ringbuff_client[fd].read_p, rb_fd) < 0)
    {
        // 读不到就两种可能：数据还没写，或者被写指针踩掉了
        if (ringbuff_client[fd].read_p == ringbuff[rb_fd].write_p)
        {
            // printf("[%s]wait write, read_p=%p, write_p=%p\n", __func__,
            //        ringbuff_client[fd].read_p, ringbuff[rb_fd].write_p);
            err = RINGBUFF_READ_REACH_WRITE;
            goto END;
        }
        else
        {
            // 指回写指针，丢掉的数据读不到就跳过
            printf("[%s]write_p cover read_p, read_p=%p, write_p=%p\n", __func__,
                   ringbuff_client[fd].read_p, ringbuff[rb_fd].write_p);
            ringbuff_client[fd].read_p = ringbuff[rb_fd].write_p;
            err = RINGBUFF_READ_P_ERR;
            goto END;
        }
    }

    if (item_info.data_size > buf_size)
    {
        printf("[%s]read buff to small\n", __func__);
        err = RINGBUFF_READ_BUFF_TOO_SMALL;
        goto END;
    }

    remain_size = ringbuff_end_p - ringbuff_client[fd].read_p;
    if (remain_size <= sizeof(ringbuff_item_info_t))
    {
        ringbuff_client[fd].read_p = ringbuff[rb_fd].ringbuff_p + sizeof(ringbuff_item_info_t) - remain_size;
    }
    else
    {
        ringbuff_client[fd].read_p += sizeof(ringbuff_item_info_t);
    }

    remain_size = ringbuff_end_p - ringbuff_client[fd].read_p;
    if (remain_size <= item_info.data_size)
    {
        memcpy(buf, ringbuff_client[fd].read_p + sizeof(ringbuff_item_info_t), remain_size);
        memcpy(buf + remain_size, ringbuff[rb_fd].ringbuff_p, item_info.data_size - remain_size);
        ringbuff_client[fd].read_p = ringbuff[rb_fd].ringbuff_p + item_info.data_size - remain_size;
    }
    else
    {
        memcpy(buf, ringbuff_client[fd].read_p, item_info.data_size);
        ringbuff_client[fd].read_p += item_info.data_size;
    }

    // 套圈
    if (item_info.data_info_p >= ringbuff_client[fd].read_p)
    {
        read_len = ringbuff_end_p - item_info.data_info_p + ringbuff_client[fd].read_p - ringbuff[fd].ringbuff_p;
    }
    else
    {
        read_len = ringbuff_client[fd].read_p - item_info.data_info_p;
    }
END:
    pthread_mutex_unlock(&(ringbuff[rb_fd].ringbuff_lock));
    if (err < 0)
    {
        return err;
    }
    return read_len;
}

void ringbuff_read_deinit(ringbuff_fd fd)
{
    if (ringbuff_client[fd].in_use)
    {
        ringbuff_client[fd].read_p = NULL;
        ringbuff_client[fd].in_use = 0;
    }
}

void ringbuff_deinit(ringbuff_fd fd)
{
    pthread_mutex_lock(&(ringbuff[fd].ringbuff_lock));
    if (ringbuff[fd].have_init == 1)
    {
        ringbuff[fd].have_init == 0;
        free(ringbuff[fd].ringbuff_p);
        free(ringbuff[fd].last_item);
    }
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));
}