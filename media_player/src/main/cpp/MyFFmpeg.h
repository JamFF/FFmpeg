//
// Created by FF on 2019-06-25.
//

#ifndef FFMPEG_MYFFMPEG_H
#define FFMPEG_MYFFMPEG_H

#include <pthread.h>
#include <android/log.h>
#include "VideoChannel.h"
#include "AudioChannel.h"

extern "C" {
#include "libavformat/avformat.h"
};

/**
 * 控制层
 */
class MyFFmpeg {

public:

    MyFFmpeg(JavaCallHelper *javaCallHelper, const char *dataSource);// 构造

    ~MyFFmpeg();// 析构

    void prepare();// 准备播放

    void prepareFFmpeg();

    void start();// 开始播放

    void getPacket();// 获取音视频数据包

    void setRenderCallback(RenderFrame);

private:

    bool isPlaying;// 是否在播放

    pthread_t pid_prepare;// FFmpeg初始化线程，初始化完成后销毁
    pthread_t pid_decode;// 解码线程，直到播放完毕后销毁

    AVFormatContext *pFormatContext;// 总上下文

    char *url;// 播放地址
    JavaCallHelper *pJavaCallHelper;// 回调Java层的接口

    VideoChannel *pVideoChannel;// 视频解码
    AudioChannel *pAudioChannel;// 音频解码

    RenderFrame renderFrame;
};


#endif //FFMPEG_MYFFMPEG_H
