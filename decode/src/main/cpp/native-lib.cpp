#include <jni.h>
#include <string>
#include <android/log.h>

// 因为FFmpeg是C编写的，需要使用extern关键字进行C和C++混合编译
extern "C" {
#include "libavcodec/avcodec.h"// 编解码
#include "libavformat/avformat.h"// 封装格式处理
#include "libswresample/swresample.h"// 冲采样
}

#define LOG_TAG "VideoPlayer"
// 方法别名
#define LOG_D(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define LOG_E(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

// 缓冲区大小，采样率乘以双通道
#define MAX_AUDIO_FRAME_SIZE (44100 * 2)

// 停止的标记位
int flag;

extern "C"
JNIEXPORT jint JNICALL
Java_com_ff_decode_DecodeUtils_decodeAudio(JNIEnv *env, jclass type, jstring input_,
                                           jstring output_) {
    const char *input = env->GetStringUTFChars(input_, nullptr);
    const char *output = env->GetStringUTFChars(output_, nullptr);

    // 1. 初始化网络模块
    avformat_network_init();

    // 总上下文
    AVFormatContext *pFormatContext = avformat_alloc_context();

    int ret;// 返回结果

    // 2.打开音频文件
    // 最后一个参数是字典，可以理解为HashMap
    ret = avformat_open_input(&pFormatContext, input, nullptr, nullptr);// 需要释放 avformat_close_input
    if (ret < 0) {
        LOG_E("无法打开输入音频文件");
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 3.获取音频文件信息
    ret = avformat_find_stream_info(pFormatContext, nullptr);
    if (ret < 0) {
        LOG_E("无法获取音频文件信息");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    int audio_stream_idx = -1;// 音频流的索引位置

    int i;
    // 遍历媒体流
    for (i = 0; i < pFormatContext->nb_streams; ++i) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // 找到视频流
            audio_stream_idx = i;
            break;
        }
    }

    if (audio_stream_idx == -1) {// 没有视频流
        LOG_E("找不到音频流");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 4.只有知道音频的编码方式，才能够根据编码方式去找到解码器
    // 音频流的解码参数
    AVCodecParameters *pParameters = pFormatContext->streams[audio_stream_idx]->codecpar;

    // 获取解码器
    AVCodec *pCodec = avcodec_find_decoder(pParameters->codec_id);
    if (pCodec == nullptr) {
        LOG_E("找不到解码器");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 解码器上下文，后面的3就是不同版本的FFmpeg函数升级后，对函数名进行了修改，作为区分
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);// 需要释放 avcodec_free_context
    if (pCodecContext == nullptr) {
        LOG_E("找不到解码器上下文");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 将解码器的参数，复制到解码器上下文
    ret = avcodec_parameters_to_context(pCodecContext, pParameters);
    if (ret < 0) {
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 5.打开解码器
    ret = avcodec_open2(pCodecContext, pCodec, nullptr);
    if (ret < 0) {
        LOG_E("解码器无法打开");
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 初始化数据包，数据都是从AVPacket中获取
    AVPacket *pPacket = av_packet_alloc();// 内存分配，需要释放 av_packet_free
    if (pPacket == nullptr) {
        LOG_E("分配AVPacket内存失败");
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // AVFrame，解码后的音频或视频的原始数据
    AVFrame *pFrame = av_frame_alloc();// 内存分配，需要释放 av_frame_free
    if (pFrame == nullptr) {
        LOG_E("分配AVPacket内存失败");
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    // 创建重采样上下文
    SwrContext *pSwrContext = swr_alloc();
    if (pSwrContext == nullptr) {
        LOG_E("分配SwrContext失败");
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    /************************************ 重采样设置参数 start ************************************/

    // 输出参数都是一致的，因为都要输出到Android喇叭中，输入参数是不一样的，根据不同的音频资源而定

    // 输入的声道布局
    uint64_t in_ch_layout = pCodecContext->channel_layout;

    // 输入的采样格式
    enum AVSampleFormat in_sample_fmt = pCodecContext->sample_fmt;

    // 输入采样率
    int in_sample_rate = pCodecContext->sample_rate;

    // 输出的声道布局（立体声，分为左右声道）
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;

    // 输出采样格式，16bit
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

    // 输出采样率，44100Hz，在一秒钟内对声音信号采样44100次
    const int out_sample_rate = 44100;

    // 根据需要分配SwrContext并设置或重置公共参数
    swr_alloc_set_opts(pSwrContext,
                       out_ch_layout, out_sample_fmt, out_sample_rate,
                       in_ch_layout, in_sample_fmt, in_sample_rate,
                       0, nullptr);// 最后两个是日志相关参数

    /************************************* 重采样设置参数 end *************************************/

    // 根据设置的参数，初始化重采样上下文
    swr_init(pSwrContext);

    // 输出缓冲区，16bit 44100 PCM 数据，16bit是2个字节
    auto *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE);

    // 输出的声道个数，其实就是2，因为上面设置了立体声
    int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
    LOG_D("out_channel_nb: %d", out_channel_nb);

    // 输出文件
    FILE *fp_pcm = fopen(output, "wb");
    if (fp_pcm == nullptr) {
        LOG_E("%s 打开失败", output);
        av_freep(out_buffer);
        swr_free(&pSwrContext);
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(input_, input);
        env->ReleaseStringUTFChars(output_, output);
        return -1;
    }

    int frame_count = 0;

    while (av_read_frame(pFormatContext, pPacket) >= 0) {// 返回下一帧
        if (pPacket->stream_index == audio_stream_idx) {// 判断音频帧
            // 7.解码一帧音频压缩数据，得到音频数据，AVPacket（压缩数据）->AVFrame（未压缩数据）

            // 7.1 将AVPacket作为输入数据，发送给解码器
            ret = avcodec_send_packet(pCodecContext, pPacket);// 输入AVPacket
            if (ret < 0) {
                LOG_E("将数据包发送到解码器时出错");
                if (ret == AVERROR(EAGAIN)) {// -11
                    // 需要avcodec_receive_frame
                    LOG_E("当前状态不接受输入, ret = %d", ret);
                } else if (ret == AVERROR_EOF) {// -541478725
                    LOG_E("已刷新解码器，并且不会向其发送新数据包, ret = %d", ret);
                    continue;
                } else if (ret == AVERROR(EINVAL)) {// -22
                    LOG_E("解码器未打开，或者它是编码器，或者需要刷新, ret = %d", ret);
                    continue;
                } else if (ret == AVERROR(ENOMEM)) {// -12
                    LOG_E("无法将数据包添加到内部队列, ret = %d", ret);
                    continue;
                } else {
                    // 第一帧会返回-1094995529，AVERROR_INVALIDDATA，因为大多是歌手信息之类
                    LOG_E("其它错误, ret = %d", ret);
                    continue;
                }
            }
            do {
                // 7.2 从解码器得到AVFrame作为输出数据，视频帧或者音频帧，取决于解码器类型
                ret = avcodec_receive_frame(pCodecContext, pFrame);// 从解码器接收帧
                if (ret < 0) {
                    LOG_E("从解码器接收帧时出错");
                    if (ret == AVERROR(EAGAIN)) {// -11
                        // 需要avcodec_send_packet
                        LOG_E("当前状态不接受输出, ret = %d", ret);
                    } else if (ret == AVERROR_EOF) {// -541478725
                        LOG_E("解码器已完全刷新，并且将不再有输出帧, ret = %d", ret);
                    } else if (ret == AVERROR(EINVAL)) {// -22
                        LOG_E("解码器未打开，或者它是编码器, ret = %d", ret);
                    } else {
                        LOG_E("其它错误, ret = %d", ret);
                    }
                    break;
                } else {
                    // pFrame中就得到了一帧音频数据，存储着解码后的PCM数据
                    LOG_D("解码：%d", ++frame_count);

                    ret = swr_convert(pSwrContext,// 重采样上下文
                                      &out_buffer,// 输出缓冲区
                                      MAX_AUDIO_FRAME_SIZE,// 每通道采样的可用空间量
                                      (const uint8_t **) pFrame->data,// 输入缓冲区
                                      pFrame->nb_samples);// 一个通道中可用的输入采样数量
                    if (ret < 0) {
                        LOG_E("转换时出错");
                    } else {
                        // 获取给定音频参数所需的缓冲区大小
                        int out_buffer_size = av_samples_get_buffer_size(nullptr,
                                                                         out_channel_nb,// 输出的声道个数
                                                                         pFrame->nb_samples,// 一个通道中音频采样数量
                                                                         out_sample_fmt,// 输出采样格式16bit
                                                                         1);// 缓冲区大小对齐（0 = 默认值，1 = 不对齐）
                        // 10.输出PCM文件，1代表缓冲区最小的单元，音频流的单位是字节，所以是1，如果是像素，那就是4
                        fwrite(out_buffer, 1, static_cast<size_t>(out_buffer_size), fp_pcm);
                    }
                }
            } while (false);// while (ret >= 0);// 这里循环的目的是：怕有未消费数据包，但是没有出现过
        }
        // 注意av_packet_alloc()这种方式初始化的，不是在这里释放
        // av_packet_free(&pPacket);
    }
    // 关闭文件
    fclose(fp_pcm);
    // 释放AVFrame
    av_frame_free(&pFrame);
    // 释放AVPacket
    av_packet_free(&pPacket);
    // 释放缓冲区
    av_freep(out_buffer);
    // 释放重采样上下文
    swr_free(&pSwrContext);
    // 关闭解码器
    avcodec_free_context(&pCodecContext);
    // 关闭AVFormatContext
    avformat_close_input(&pFormatContext);
    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);

    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_ff_decode_DecodeUtils_decodeVideo(JNIEnv *env, jclass type, jstring input_,
                                           jstring output_) {
    const char *input = env->GetStringUTFChars(input_, nullptr);
    const char *output = env->GetStringUTFChars(output_, nullptr);

    // TODO

    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);

    return 0;
}