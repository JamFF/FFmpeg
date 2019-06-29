//
// Created by FF on 2019-06-26.
//

#include "BaseChannel.h"

BaseChannel::BaseChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext)
        : channelId(id),
          javaCallHelper(javaCallHelper),
          avCodecContext(avCodecContext) {
    // TODO 参数是一个模版
    pkt_queue.setReleaseHandle(releaseAvPacket);// releaseAvPacket需要声明为静态，不然调用报错
    frame_queue.setReleaseHandle(releaseAvFrame);// releaseAvFrame需要声明为静态，不然调用报错
}

// 头文件中声明virtual即可，在cpp中声明会报错
BaseChannel::~BaseChannel() {
    if (avCodecContext) {
        avcodec_close(avCodecContext);
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    pkt_queue.clear();
    frame_queue.clear();
    LOG_D("释放channel:%d %d", pkt_queue.size(), frame_queue.size());
}

void BaseChannel::releaseAvPacket(AVPacket *&packet) {
    // TODO 参数 *& 是指针的引用，不理解
    if (packet != nullptr) {
        av_packet_free(&packet);
        packet = nullptr;
    }
}

void BaseChannel::releaseAvFrame(AVFrame *&frame) {
    if (frame != nullptr) {
        av_frame_free(&frame);
        frame = nullptr;
    }
}
