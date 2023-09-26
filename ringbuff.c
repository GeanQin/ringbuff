#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "ringbuff.h"

static ringbuff_t ringbuff[RINGBUFF_COUNT_MAX];
static ringbuff_client_t ringbuff_client[RINGBUFF_CLIENT_COUNT_MAX];
static u_int64_t write_seq[RINGBUFF_COUNT_MAX];
static u_int64_t pre_read_seq[RINGBUFF_CLIENT_COUNT_MAX];
static int8_t get_item_info(int8_t to_before, ringbuff_item_info_t *item_info, uint8_t *read_p, ringbuff_fd fd, ringbuff_read_fd read_fd)
{
    int i = 0;
    int64_t remain_len = 0;
    u_int8_t *ringbuff_end_p = NULL;
    u_int8_t *item_head = (u_int8_t *)item_info;

    if (read_p == NULL)
    {
        fprintf(stderr, "[%s]read_p is NULL\n", __func__);
        return -1;
    }

    if (read_p == ringbuff[fd].write_p)
    {
        // fprintf(stderr, "[%s]read_p == write_p, wait....\n", __func__);
        return -1;
    }

    ringbuff_end_p = ringbuff[fd].ringbuff_p + ringbuff[fd].ringbuff_size;
    remain_len = ringbuff_end_p - read_p;
    
    if (remain_len <= sizeof(ringbuff_item_info_t))
    {
        // printf("[%s]splice item_info! read_p=%p remain_len=%ld\n", __func__, read_p, remain_len);
        memcpy(item_head, read_p, remain_len);
        memcpy(item_head + remain_len, ringbuff[fd].ringbuff_p, sizeof(ringbuff_item_info_t) - remain_len);
        printf("=================back====================\n");
        printf("p len=%ld\n", item_info->data_size);
        printf("p pre_len=%ld\n", item_info->pre_data_size);
        printf("p separator=[0x%02x, 0x%02x]\n", item_info->separator[0], item_info->separator[7]);
        printf("p seq=%lu\n", item_info->seq);
        printf("=======================================\n");
    }
    else
    {
        memcpy(item_info, read_p, sizeof(ringbuff_item_info_t));
    }

    if (item_info->data_info_p > ringbuff_end_p || ringbuff_end_p < ringbuff[fd].ringbuff_p)
    {
        fprintf(stderr, "[%s]read_p err\n", __func__);
        return -1;
    }

    if (item_info->data_size < 0 || item_info->data_size + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        fprintf(stderr, "[%s]data size err\n", __func__);
        return -1;
    }

    if (item_info->pre_data_size < 0 || item_info->pre_data_size + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        fprintf(stderr, "[%s]last data size err\n", __func__);
        return -1;
    }

    for (i = 0; i < sizeof(item_info->separator); i++)
    {
        if (item_info->separator[i] != 1)
        {
            fprintf(stderr, "[%s]separator is not 11111111\n", __func__);
            return -1;
        }
    }

#if 0
    printf("=======================================\n");
    printf("ringbuff%d\n", fd);
    printf("data_info_p: %p\n", item_info->data_info_p);
    printf("data_size: %lu\n", item_info->data_size);
    printf("pre_data_size: %lu\n", item_info->pre_data_size);
    printf("seq: %lu\n", item_info->seq);
    printf("separator[0]: %02x\n", item_info->separator[0]);
    printf("separator[7]: %02x\n", item_info->separator[7]);
    printf("=======================================\n");
#endif

    // 防止读到之前的数据了
    if (to_before == 0)
    {
        if (item_info->seq != 0 && item_info->seq < pre_read_seq[read_fd])
        {
            fprintf(stderr, "[%s]have read before data\n", __func__);
            return -1;   
        }
        pre_read_seq[read_fd] = item_info->seq;
    }

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
    write_seq[use_index] = 0;
    ringbuff[use_index].have_init = 1;

    printf("==============================================\n");
    printf("[%s] use %d\n", __func__, use_index);
    printf("[%s] ringbuff_p=%p\n", __func__, ringbuff[use_index].ringbuff_p);
    printf("[%s] ringbuff_end_p=%p\n", __func__, ringbuff[use_index].ringbuff_p + size);
    printf("==============================================\n");

    return use_index;
}

ringbuff_fd ringbuff_get_fd(char *ringbuff_tag)
{
    int i = 0;
    int index = -1;

    for (i = 0; i < RINGBUFF_COUNT_MAX; i++)
    {
        if (ringbuff[i].have_init == 1 &&
            strncmp(ringbuff_tag, ringbuff[i].ringbuff_tag, strlen(ringbuff_tag) + 1) == 0)
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

/**
 * |头|数据|
 * 头：记录数据位置
 * 数据：任意数据
 */
int64_t ringbuff_put(ringbuff_fd fd, u_int8_t *data, int64_t data_len)
{
    u_int8_t *ringbuff_end_p = NULL;
    u_int8_t *item_head = NULL;
    int64_t remain_size = 0;
    int64_t write_len = 0;

    if (ringbuff[fd].have_init == 0)
    {
        fprintf(stderr, "[%s] ringbuff%d is not init\n", __func__, fd);
        return RINGBUFF_DO_NOT_INIT;
    }

    if (data_len + sizeof(ringbuff_item_info_t) > ringbuff[fd].ringbuff_size)
    {
        fprintf(stderr, "[%s] ringbuff%d is not enough\n", __func__, fd);
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
    ringbuff[fd].last_item->seq = write_seq[fd];
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
    write_seq[fd]++;

#if 0
    printf("==========================\n");
    printf("ringbuff idx=%d\n", fd);
    printf("ringbuff start=%p\n", ringbuff[fd].ringbuff_p);
    printf("ringbuff len=%ld\n", ringbuff[fd].ringbuff_size);
    printf("ringbuff last pre_len=%ld\n", ringbuff[fd].last_item->pre_data_size);
    printf("ringbuff last p=%p\n", ringbuff[fd].last_item->data_info_p);
    printf("ringbuff last len=%ld\n", ringbuff[fd].last_item->data_size);
    printf("ringbuff last seq=%llu\n", ringbuff[fd].last_item->seq);
    printf("ringbuff w=%p\n", ringbuff[fd].write_p);
    printf("ringbuff seq=%llu\n", write_seq[fd]);
    printf("==========================\n");
#endif
    pthread_mutex_unlock(&(ringbuff[fd].ringbuff_lock));

    return write_len - sizeof(ringbuff_item_info_t);
}

void ringbuff_dump(ringbuff_fd fd)
{
    char filename[512] = {0};
    FILE *fp = NULL;

    snprintf(filename, sizeof(filename), "ringbuff-%s-offset%lu",
             ringbuff[fd].ringbuff_tag, ringbuff[fd].last_item->data_info_p - ringbuff[fd].ringbuff_p);

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

    if (get_item_info(1, &item_info, ringbuff_client[use_index].read_p, fd, use_index) < 0)
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
        if (get_item_info(1, &item_info, ringbuff_client[use_index].read_p, fd, use_index) < 0)
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

    pre_read_seq[use_index] = 0;
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

    rb_fd = ringbuff_client[fd].rb_fd;
    ringbuff_end_p = ringbuff[rb_fd].ringbuff_p + ringbuff[rb_fd].ringbuff_size;

    pthread_mutex_lock(&(ringbuff[rb_fd].ringbuff_lock));
    if (ringbuff[rb_fd].have_init == 0)
    {
        err = RINGBUFF_DO_NOT_INIT;
        goto END;
    }

    if (get_item_info(0, &item_info, ringbuff_client[fd].read_p, rb_fd, fd) < 0)
    {
        // 读不到就两种可能：数据还没写，或者被写指针踩掉了
        if (ringbuff_client[fd].read_p == ringbuff[rb_fd].write_p)
        {
            err = RINGBUFF_READ_REACH_WRITE;
            goto END;
        }
        else
        {
            // 被覆盖，指回写指针，丢掉的数据读不到就跳过
            fprintf(stderr, "ringbuff%d star_p(%p) end_p(%p)\n", rb_fd, ringbuff[rb_fd].ringbuff_p, ringbuff[rb_fd].ringbuff_p + ringbuff[rb_fd].ringbuff_size);
            fprintf(stderr, "[%s]ringbuff%d, write_p(%p) cover read_p(%p), last_p(%p), last_last_p(%p)\n",
                     __func__, rb_fd, ringbuff[rb_fd].write_p, ringbuff_client[fd].read_p, ringbuff[rb_fd].last_item->data_info_p, 
                     ringbuff[rb_fd].last_item->data_info_p - ringbuff[rb_fd].last_item->pre_data_size - sizeof(ringbuff_item_info_t));

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

    // read_p向前推过头部ringbuff_item_info_t
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
        memcpy(buf, ringbuff_client[fd].read_p, remain_size);
        memcpy(buf + remain_size, ringbuff[rb_fd].ringbuff_p, item_info.data_size - remain_size);
        ringbuff_client[fd].read_p = ringbuff[rb_fd].ringbuff_p + item_info.data_size - remain_size;
    }
    else
    {
        memcpy(buf, ringbuff_client[fd].read_p, item_info.data_size);
        ringbuff_client[fd].read_p += item_info.data_size;
    }
#if 0
    printf("=================read====================\n");
    printf("p len=%ld\n", item_info.data_size);
    printf("p pre_len=%ld\n", item_info.pre_data_size);
    printf("p separator=[0x%02x, 0x%02x]\n", item_info.separator[0], item_info.separator[7]);
    printf("p seq=%lu\n", item_info.seq);
    printf("=======================================\n");
#endif
END:
    pthread_mutex_unlock(&(ringbuff[rb_fd].ringbuff_lock));
    if (err < 0)
    {
        return err;
    }
    return item_info.data_size;
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