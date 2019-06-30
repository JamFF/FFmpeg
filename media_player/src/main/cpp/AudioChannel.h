//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_AUDIOCHANNEL_H
#define FFMPEG_AUDIOCHANNEL_H

#include <SLES/OpenSLES_Android.h>
#include "BaseChannel.h"

extern "C" {
#include <libswresample/swresample.h>
}

class AudioChannel : public BaseChannel {

public:
    AudioChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext);

    void start();// 开启解码和播放线程

    void stop();

    void initOpenSL();// 初始化OpenSL

    unsigned int getPCM();// PCM重采样

    uint8_t *buffer;// 输出缓冲区

private:
    pthread_t pid_audio_decode;// 解码线程
    pthread_t pid_audio_play;// OpenSL ES初始化线程
    SwrContext *swr_ctx;// 转换上下文
    int out_channels;// 通道数，例如双通道
    int out_sample_size;// 采样位数，例如16bit
    int out_sample_rate;// 输出采样率，例如44100Hz，一秒内对声音信号采样44100次
};

#endif //FFMPEG_AUDIOCHANNEL_H
