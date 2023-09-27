#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "libavformat/avformat.h"
#include "libavutil/mathematics.h"

#include "mp4_record.h"

#define MAX_VIDEO_SPS_PPS_LEN (4096 + 4096) //  Get from ffmpeg, max sps len:4096  max pps len: 4096
#define MAX_AUDIO_EXTRADATA_LEN (1024)

static AVFormatContext *mp4_ctx = NULL;
static AVStream *ai_stream = NULL;
static AVStream *vi_stream = NULL;
static pthread_mutex_t write_lock;
static int64_t vi_last_ts = -1;
static int64_t ai_last_ts = -1;

static void print_pkt(AVPacket *pkt)
{
    printf("data=%p\n", pkt->data);
    printf("size=%d\n", pkt->size);
    printf("dts=%lld\n", pkt->dts);
    printf("duration=%lld\n", pkt->duration);
    printf("flags=%d\n", pkt->flags);
    printf("pos=%lld\n", pkt->pos);
    printf("pts=%lld\n", pkt->pts);
    printf("stream_index=%d\n\n\n", pkt->stream_index);
}

static int video_find_extradata(frame_info_t *frame, unsigned char *extradata, int extradata_size)
{
    int i = 0;
#if 0   // print
    for(i = 0; i < 512; i++)
    {
        if (i % 15 == 0)
        {
            printf("\n");
        }
        printf("%02x ", frame->data[i]);
    }
    printf("\n");
#endif
    if (frame->venc_type == VENC_FORMAT_H265)
    {
        // H265的I帧以0x00 0x00 0x00 0x01 0x40开头,
        // 0x40 0x01是VPS(视频参数集),
        // 0x42 0x01是SPS(序列参数集),
        // 0x44 0x01是PPS(图像参数集),
        // 0x4e 0x01是SEI(补充增强信息)
        if (frame->data[0] == 0x00 &&
            frame->data[1] == 0x00 &&
            frame->data[2] == 0x00 &&
            frame->data[3] == 0x01 &&
            frame->data[4] == 0x40)
        {
            for (i = 5; i < frame->data_len; i++)
            {
                // 0x26 0x01是I帧开头
                if (frame->data[i - 1] == 0x26 && frame->data[i] == 0x01)
                {
                    memcpy(extradata, frame->data, i - 1);
                    return i - 1;
                }
            }
        }
        else
        {
            fprintf(stderr, "create_mp4 h265 first frame is invaild!\n");
            return -1;
        }
    }
    else if (frame->venc_type == VENC_FORMAT_H264)
    {
        // H264的I帧以0x00 0x00 0x01 0x27开头,
        // h265比h264多了一个VPS
        // 0x27 0x01是SPS(序列参数集),
        // 0x28 0x01是PPS(图像参数集),
        // 0x06 0x01是SEI(补充增强信息)
        if (frame->data[0] == 0x00 &&
            frame->data[1] == 0x00 &&
            frame->data[2] == 0x00 &&
            frame->data[3] == 0x01 &&
            frame->data[4] == 0x27)
        {
            for (i = 5; i < frame->data_len; i++)
            {
                // 0x65是I帧开头
                if (frame->data[i] == 0x25)
                {
                    memcpy(extradata, frame->data, i);
                    return i;
                }
            }
        }
        else
        {
            fprintf(stderr, "create_mp4 h264 first frame is invaild!\n");
            return -1;
        }
    }
    else
    {
        fprintf(stderr, "create_mp4 frame format is %d, do not support\n", frame->venc_type);
    }

    return -1;
}

static int audio_make_extradata(aenc_format_e audio_format, unsigned char *extradata, int extradata_size)
{
    unsigned char dsi[2];
    unsigned int object_type = 2; // AAC LC by default
    int sampling_frequency_index = -1;

    if (audio_format == AENC_FORMAT_AAC)
    {
        switch (AI_SAMPLE_RATE)
        {
        case 16000:
            sampling_frequency_index = 0x8;
            break;
        case 8000:
            sampling_frequency_index = 0xb;
        default:
            fprintf(stderr, "create_mp4 AI_SAMPLE_RATE err!\n");
            return -1;
        }
        dsi[0] = (object_type << 3) | (sampling_frequency_index >> 1);
        dsi[1] = ((sampling_frequency_index & 0x1) << 7) | (AI_CHANNELS << 3);
        memcpy(extradata, dsi, sizeof(dsi));
        return sizeof(dsi);
    }

    return -1;
}

int ffmpeg_create_mp4(char *file_name, frame_info_t *frame, aenc_format_e audio_format)
{
    int ret = 0;
    enum AVCodecID venc_id = AV_CODEC_ID_NONE;
    enum AVCodecID aenc_id = AV_CODEC_ID_NONE;
    AVOutputFormat *ofmt = NULL;
    AVCodec *vi_codec = NULL;
    AVCodec *ai_codec = NULL;
    AVCodecContext *vi_codec_ctx = NULL;
    AVCodecContext *ai_codec_ctx = NULL;
    AVCodecParameters vi_codec_para;
    AVCodecParameters ai_codec_para;

    if (file_name == NULL || frame == NULL)
    {
        fprintf(stderr, "create_mp4 param err!\n");
        return -1;
    }

    switch (frame->venc_type)
    {
    case VENC_FORMAT_H265:
        venc_id = AV_CODEC_ID_H265;
        break;
    case VENC_FORMAT_H264:
        venc_id = AV_CODEC_ID_H264;
        break;

    default:
        fprintf(stderr, "create_mp4 do not support this video format!\n");
        return -1;
    }

    switch (audio_format)
    {
    case AENC_FORMAT_AAC:
        aenc_id = AV_CODEC_ID_AAC;
        break;

    default:
        fprintf(stderr, "create_mp4 do not support this audio format!\n");
        return -1;
    }

    ret = avformat_alloc_output_context2(&mp4_ctx, NULL, NULL, file_name);
    if (ret < 0 || mp4_ctx == NULL)
    {
        fprintf(stderr, "create_mp4 file err\n%s\n", av_err2str(ret));
        return -1;
    }
    ofmt = mp4_ctx->oformat;
    ofmt->video_codec = venc_id;
    ofmt->audio_codec = aenc_id;

#if 1   // video
    if (!(vi_codec = avcodec_find_decoder(venc_id)))
    {
        fprintf(stderr, "create_mp4 can not find video encoder\n");
        return -1;
    }

    vi_stream = avformat_new_stream(mp4_ctx, vi_codec);
    if (!vi_stream)
    {
        fprintf(stderr, "create_mp4 failed allocating video output stream\n");
        return -1;
    }

    vi_codec_ctx = avcodec_alloc_context3(vi_codec);
    if (!vi_codec_ctx)
    {
        fprintf(stderr, "create_mp4 alloc video codec ctx err\n");
        return -1;
    }

    vi_codec_para.codec_id = venc_id;
    vi_codec_para.codec_type = AVMEDIA_TYPE_VIDEO;
    vi_codec_para.format = AV_PIX_FMT_YUV420P;
    vi_codec_para.width = frame->width;
    vi_codec_para.height = frame->height;
    vi_codec_para.sample_aspect_ratio.den = 1;
    vi_codec_para.sample_aspect_ratio.num = 0;

    vi_codec_para.extradata = (uint8_t *)malloc(MAX_VIDEO_SPS_PPS_LEN);
    vi_codec_para.extradata_size = video_find_extradata(frame, vi_codec_para.extradata, MAX_VIDEO_SPS_PPS_LEN);
    if (vi_codec_para.extradata_size < 0)
    {
        fprintf(stderr, "create_mp4 make extradata err\n");
        return -1;
    }

    ret = avcodec_parameters_to_context(vi_codec_ctx, &vi_codec_para);
    if (ret < 0)
    {
        fprintf(stderr, "create_mp4 failed to copy vi_stream codecpar to codec context, %s\n", av_err2str(ret));
        return -1;
    }
    free(vi_codec_para.extradata);

    vi_codec_ctx->codec_tag = 0;
    if (mp4_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        vi_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_parameters_from_context(vi_stream->codecpar, vi_codec_ctx);
    if (ret < 0)
    {
        fprintf(stderr, "create_mp4 failed to copy codec context to out_stream codecpar context\n");
        return -1;
    }
#endif
#if 1   // audio
    if (!(ai_codec = avcodec_find_decoder(aenc_id)))
    {
        fprintf(stderr, "create_mp4 can not find audio encoder\n");
        return -1;
    }

    ai_stream = avformat_new_stream(mp4_ctx, ai_codec);
    if (!ai_stream)
    {
        fprintf(stderr, "create_mp4 failed allocating output audio stream\n");
        return -1;
    }

    ai_codec_ctx = avcodec_alloc_context3(ai_codec);
    if (!ai_codec_ctx)
    {
        fprintf(stderr, "create_mp4 alloc audio codec ctx err\n");
        return -1;
    }

    ai_codec_para.codec_id = aenc_id;
    ai_codec_para.codec_type = AVMEDIA_TYPE_AUDIO;
    ai_codec_para.sample_rate = AI_SAMPLE_RATE;
    ai_codec_para.channels = AI_CHANNELS;
    ai_codec_para.channel_layout = AV_CH_LAYOUT_MONO;  //  AUDIO_SOUND_MODE_MONO
    ai_codec_para.frame_size = 1024;    // faac input_len
    ai_codec_para.extradata = (uint8_t *)malloc(MAX_AUDIO_EXTRADATA_LEN);
    ai_codec_para.extradata_size = audio_make_extradata(audio_format, ai_codec_para.extradata, MAX_AUDIO_EXTRADATA_LEN);
    ret = avcodec_parameters_to_context(ai_codec_ctx, &ai_codec_para);
    if (ret < 0)
    {
        fprintf(stderr, "create_mp4 failed to copy ai_stream codecpar to codec context\n%s\n", av_err2str(ret));
        return -1;
    }
    free(ai_codec_para.extradata);

    ai_codec_ctx->codec_tag = 0;
    if (mp4_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ai_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_parameters_from_context(ai_stream->codecpar, ai_codec_ctx);
    if (ret < 0)
    {
        fprintf(stderr, "create_mp4 failed to copy codec context to out_stream codecpar context\n");
        return -1;
    }
#endif
    // 打印看看
    av_dump_format(mp4_ctx, 0, file_name, 1);

    // Open output file
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        if (avio_open(&mp4_ctx->pb, file_name, AVIO_FLAG_WRITE) < 0)
        {
            fprintf(stderr, "Could not open output file '%s'\n", file_name);
            return -1;
        }
    }
    // Write file header
    if (avformat_write_header(mp4_ctx, NULL) < 0)
    {
        fprintf(stderr, "Error occurred when opening output file\n");
        return -1;
    }

    pthread_mutex_init(&(write_lock), NULL);

    return 0;
}

int ffmpeg_write_mp4(frame_info_t *frame)
{
    int i = 0, ret = 0;
    AVPacket pkt;

    if (mp4_ctx == NULL || vi_stream == NULL || ai_stream == NULL)
    {
        fprintf(stderr, "[%s]please create mp4 first\n", __func__);
        return -1;
    }

    pthread_mutex_lock(&write_lock);

    av_init_packet(&pkt);

    if (frame->type == FRAME_TYPE_VIDEO)
    {
        // printf("============================video=================================\n");
        pkt.stream_index = vi_stream->index;
        // 没有b帧所以采用pts=dts
        pkt.pts = frame->ts;
        pkt.dts = frame->ts;
        if (vi_last_ts <= 0)
        {
            pkt.duration = 1000 / VIDEO_FPS * 1000;
        }
        else
        {
            pkt.duration = frame->ts - vi_last_ts;
        }

        AVRational tb;
        tb.den = 1000000;
        tb.num = 1;
        pkt.pts = av_rescale_q_rnd(pkt.pts, tb, vi_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, tb, vi_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, tb, vi_stream->time_base);

        if (frame->key_frame == 1)
        {
            pkt.flags = AV_PKT_FLAG_KEY;
        }

        vi_last_ts = frame->ts;
    }
    else if (frame->type == FRAME_TYPE_AUDIO)
    {
        // printf("============================audio=================================\n");
        pkt.stream_index = ai_stream->index;
        pkt.pts = frame->ts;
        pkt.dts = frame->ts;
        if (ai_last_ts <= 0)
        {
            pkt.duration = 1000 / AI_FPS * 1000;
        }
        else
        {
            pkt.duration = frame->ts - ai_last_ts;
        }
        AVRational tb;
        tb.den = 1000000;
        tb.num = 1;
        pkt.pts = av_rescale_q_rnd(pkt.pts, tb, ai_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, tb, ai_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, tb, ai_stream->time_base);

        ai_last_ts = frame->ts;
    }

    pkt.size = frame->data_len;
    pkt.data = frame->data;

    // print_pkt(&pkt);
    if (av_interleaved_write_frame(mp4_ctx, &pkt) < 0)
    {
        fprintf(stderr, "Error muxing packet\n");
        return -1;
    }
    // av_packet_unref(&pkt);

    pthread_mutex_unlock(&write_lock);

    // printf("============================end=================================\n");

    return 0;
}

void ffmpeg_stop_mp4()
{
    if (mp4_ctx == NULL)
    {
        return;
    }
    pthread_mutex_lock(&(write_lock));
    av_write_trailer(mp4_ctx);
    pthread_mutex_unlock(&write_lock);
    pthread_mutex_destroy(&write_lock);

    if (mp4_ctx && !(mp4_ctx->oformat->flags & AVFMT_NOFILE))
        avio_close(mp4_ctx->pb);

    avformat_free_context(mp4_ctx);

    mp4_ctx = NULL;
    ai_stream = NULL;
    vi_stream = NULL;
    vi_last_ts = -1;
    ai_last_ts = -1;
}