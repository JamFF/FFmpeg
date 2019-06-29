//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_VIDEOCHANNEL_H
#define FFMPEG_VIDEOCHANNEL_H

#include "BaseChannel.h"

// 定义接口
typedef void (*RenderFrame)(uint8_t *, int, int, int);

class VideoChannel : public BaseChannel {

public:
    VideoChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext);

    void start();

    void stop();

    void decodePacket();

    void synchronizeFrame();

    void setRenderCallback(RenderFrame renderFrame);

private:
    pthread_t pid_video_play;// 解码线程
    pthread_t pid_synchronize;// 播放线程
    RenderFrame renderFrame;
};


#endif //FFMPEG_VIDEOCHANNEL_H
