#ifndef __RINGBUFF_H__
#define __RINGBUFF_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <sys/types.h>
#include <pthread.h>

#define RINGBUFF_COUNT_MAX 5
#define RINGBUFF_CLIENT_COUNT_MAX 5

typedef int ringbuff_fd;
typedef int ringbuff_read_fd;

typedef enum
{
    RINGBUFF_NONE_ERR = 0,
    RINGBUFF_DO_NOT_INIT = -100,
    RINGBUFF_ALL_IN_USE,
    RINGBUFF_MALLOC_ERR,
    RINGBUFF_CANNOT_FIND,
    RINGBUFF_WRITE_P_ERR,
    RINGBUFF_INPUT_DATA_ERR,
    RINGBUFF_CLIENT_ALL_IN_USE,
    RINGBUFF_READ_P_ERR,
    RINGBUFF_READ_BUFF_TOO_SMALL,
    RINGBUFF_READ_REACH_WRITE
} ringbuff_err_e;

typedef struct
{
    u_int8_t *data_info_p;
    int64_t data_size;
    int64_t pre_data_size;
    u_int64_t seq;
    u_int8_t separator[8];  // 11111111
} ringbuff_item_info_t;

typedef struct
{
    char ringbuff_tag[16];
    u_int8_t have_init;
    u_int8_t *ringbuff_p;
    int64_t ringbuff_size;
    u_int8_t *write_p;
    ringbuff_item_info_t *last_item;
    pthread_mutex_t ringbuff_lock;
} ringbuff_t;

typedef struct
{
    ringbuff_fd rb_fd;
    u_int8_t in_use;
    u_int8_t *read_p;
} ringbuff_client_t;

/*
 * 初始化ringbuff
 * @param[in]
 *      ringbuff_tag                ringbuff标识
 * @param[in]
 *      size                        ringbuff大小
 * @retval
 *      0 ~ RINGBUFF_COUNT_MAX - 1  成功, 返回值是ringbuff的描述符
 * @retval
 *      <0                          失败
 */
ringbuff_fd ringbuff_init(char *ringbuff_tag, int64_t size);
/*
 * 释放ringbuff
 * @param[in]
 *      index                       ringbuff的描述符
 */
void ringbuff_deinit(ringbuff_fd fd);
/*
 * 查询ringbuff的描述符
 * @param[in]
 *      ringbuff_tag                ringbuff标识
 * @retval
 *      0 ~ RINGBUFF_COUNT_MAX - 1  成功, 返回值是ringbuff的描述符
 * @retval
 *      <0                          失败
 */
ringbuff_fd ringbuff_get_fd(char *ringbuff_tag);
/*
 * 重置ringbuff
 * @param[in]
 *      index                       ringbuff的描述符
 */
void ringbuff_reset(ringbuff_fd fd);
/*
 * 写入ringbuff
 * @param[in]
 *      index                       ringbuff的描述符
 * @param[in]
 *      data                        数据buff
 * @param[in]
 *      data_len                    data大小
 * @retval
 *      >0                          成功, 返回值为真实写入ringbuff的长度 = data_len + sizeof(ringbuff_item_info_t)
 * @retval
 *      <0                          失败
 */
int64_t ringbuff_put(ringbuff_fd fd, u_int8_t *data, int64_t data_len);
/*
 * 将ringbuff存至文件，程序运行目录下
 * @param[in]
 *      index                       ringbuff的描述符
 */
void ringbuff_dump(ringbuff_fd fd);
/*
 * 初始化ringbuff读取客户端, 一个线程一个客户端，线程不安全
 * @param[in]
 *      fd                                  ringbuff的描述符
 * @param[in]
 *      forward                             偏移至写指针前多少帧
 * @retval
 *      0 ~ RINGBUFF_CLIENT_COUNT_MAX - 1   成功, 返回值是ringbuff client的描述符
 * @retval
 *      <0                                  失败
 */
ringbuff_read_fd ringbuff_read_init(ringbuff_fd fd, u_int32_t forward);
/*
 * ringbuff读取，读不到就是读指针被写指针踩了，读指针会重新指向写指针
 * @param[in]
 *      fd                          ringbuff client的描述符
 * @param[out]
 *      buf                         存放读取的data
 * @param[in]
 *      buf_size                    buf的大小
 * @retval
 *      >0                          成功, 返回值读取到的长度
 * @retval
 *      <0                          失败
 */
int64_t ringbuff_read(ringbuff_read_fd fd, u_int8_t *buf, int64_t buf_size);
/*
 * 结束ringbuff读取
 * @param[in]
 *      index                       ringbuff client的描述符
 */
void ringbuff_read_deinit(ringbuff_fd fd);


#ifdef __cplusplus
}
#endif

#endif