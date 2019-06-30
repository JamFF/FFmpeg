//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_VIDEOCHANNEL_H
#define FFMPEG_VIDEOCHANNEL_H

#include "AudioChannel.h"

// 定义接口
typedef void (*RenderFrame)(uint8_t *, int, int, int);

class VideoChannel : public BaseChannel {

public:
    VideoChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext,
                 AVRational time_base);

    void start();// 开启解码和播放线程

    void stop();

    void synchronizeFrame();

    void setRenderCallback(RenderFrame renderFrame);

    void setFPS(double fps);

    AudioChannel *pAudioChannel;// 引入目的是为了于音频同步

private:
    pthread_t pid_video_decode;// 解码线程
    pthread_t pid_synchronize;// 播放线程
    RenderFrame renderFrame;// 回调native-lib的接口，将数据发送给ANativeWindow渲染
    double fps;// 帧率
};

#endif //FFMPEG_VIDEOCHANNEL_H
