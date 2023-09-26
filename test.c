#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "ringbuff.h"

#define RINGBUFF_SIZE 5120

ssize_t get_file_content(char *file_name, uint8_t **out_buf)
{
    int fd;
    size_t file_size;
    ssize_t read_size;

    fd = open(file_name, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open %s!\n", file_name);
        return -1;
    }

    file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    *out_buf = (uint8_t *)malloc(sizeof(uint8_t) * file_size);

    read_size = read(fd, *out_buf, file_size);
    if (read_size != file_size)
    {
        fprintf(stderr, "Have not get all connect from %s!\n", file_name);
        return -1;
    }

    close(fd);
    return read_size;
}

int get_one_ADTS_frame(unsigned char *buffer, size_t buf_size, unsigned char *data, size_t *data_size)
{
    size_t size = 0;

    if (!buffer || !data || !data_size)
    {
        return -1;
    }

    while (1)
    {
        if (buf_size < 7)
        {
            return -2;
        }

        if ((buffer[0] == 0xff) && ((buffer[1] & 0xf0) == 0xf0))
        {
            size |= ((buffer[3] & 0x03) << 11); // high 2 bit
            size |= buffer[4] << 3;             // middle 8 bit
            size |= ((buffer[5] & 0xe0) >> 5);  // low 3bit
            break;
        }
        --buf_size;
        ++buffer;
    }

    if (buf_size < size)
    {
        return -3;
    }

    memcpy(data, buffer, size);
    *data_size = size;

    return 0;
}

static void *data_get(void *arg)
{
    ringbuff_fd *fd = (ringbuff_fd *)arg;
    ringbuff_read_fd client_fd = -1;
    u_int8_t data[1024] = {0};
    u_int64_t get_len = 0;
    FILE *fp = NULL;

    pthread_detach(pthread_self());

    sleep(2);

    client_fd = ringbuff_read_init(*fd, 5);

    fp = fopen("out.aac", "w");
    if (fp == NULL)
    {
        fprintf(stderr, "cannot open out.aac\n");
        return NULL;
    }

    while (1)
    {
        get_len = ringbuff_read(client_fd, data, sizeof(data));
        if (get_len > 0 && get_len < RINGBUFF_SIZE)
        {
            fwrite(data, 1, get_len, fp);
            fflush(fp);
        }
        usleep(10 * 1000);
    }
}

// #define TRY_TIMES 1000
int main()
{
    int ret = 0;
    ringbuff_fd fd = -1;
    uint8_t *aac_buf = NULL;
    ssize_t aac_buf_len = 0;
    unsigned char frame[1024] = {0};
    unsigned long frame_len = 0;
    unsigned char *input_data = NULL;
    size_t input_data_len = 0;
    u_int64_t ringbuff_write_len = 0;
    pthread_t pid;
#ifdef TRY_TIMES
    int run_count = TRY_TIMES;
#endif

    fd = ringbuff_init("test", RINGBUFF_SIZE);
    if (fd < 0 || fd >= RINGBUFF_COUNT_MAX)
    {
        printf("ringbuff_init err ret=%d\n", fd);
        return ret;
    }

    aac_buf_len = get_file_content("test.aac", &aac_buf);
    if (aac_buf_len <= 0)
    {
        printf("cannot read test.aac\n");
        return -1;
    }

    pthread_create(&pid, 0, data_get, &fd);

    input_data = aac_buf;
    input_data_len = aac_buf_len;
    while (1)
    {
        if (get_one_ADTS_frame(input_data, input_data_len, frame, &frame_len) == 0)
        {
            ringbuff_write_len = ringbuff_put(fd, frame, frame_len);
            if (ringbuff_write_len != frame_len)
            {
                printf("ringbuff_put err, write_len=%lu, data_len=%lu\n", ringbuff_write_len, frame_len);
                ringbuff_dump(fd);
                break;
            }
            else
            {
                // printf("write_len=%lu, data_len=%lu\n", ringbuff_write_len, frame_len);
            }
            input_data_len -= frame_len;
            input_data += frame_len;
        }
        else
        {
            input_data = aac_buf;
            input_data_len = aac_buf_len;
        }
        usleep(20 * 1000);
#ifdef TRY_TIMES
        run_count--;
        if (run_count < 0)
        {
            ringbuff_dump(fd);
            break;
        }
#endif
    }

    free(aac_buf);

    return ret;
}