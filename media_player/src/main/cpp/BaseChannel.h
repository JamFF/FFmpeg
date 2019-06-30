//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_BASECHANNEL_H
#define FFMPEG_BASECHANNEL_H

#include "safe_queue.h"
#include "JavaCallHelper.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/time.h"// av_usleep使用
};

const int QUEUE_MAX = 100;// 队列最大数

/**
 * 解码音视频的抽象类（带有纯虚函数的类称为抽象类）
 */
class BaseChannel {

public:

    BaseChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext);

    virtual ~BaseChannel();// 父类的析构函数需要定义为虚函数，避免内存泄漏

    static void releaseAvPacket(AVPacket *&packet);// 释放，必须声明为静态，不然调用报错

    static void releaseAvFrame(AVFrame *&frame);// 释放，需要声明为静态，不然调用报错

    // 纯虚函数格式："virtual 函数类型 函数名 (参数表列) = 0;"
    // 通知编译系统：在这里声明一个纯虚函数，它的实现留给该基类的派生类去做
    virtual void start() = 0;// 开始
    virtual void stop() = 0;// 停止

    void decodePacket();// 解码音频、视频，由子类调用，运行在子线程

    SafeQueue<AVPacket *> pkt_queue;// AVPacket队列
    SafeQueue<AVFrame *> frame_queue;// AVFrame队列
    volatile int channelId;
    volatile bool isPlaying;
    AVCodecContext *avCodecContext;// 析构方法中通过avcodec_free_context释放
    JavaCallHelper *javaCallHelper;// 回调Java的接口
};

#endif //FFMPEG_BASECHANNEL_H
