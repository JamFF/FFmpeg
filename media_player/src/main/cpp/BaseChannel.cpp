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

/**
 * 解码音频、视频，由子类调用，运行在子线程
 */
void BaseChannel::decodePacket() {
    AVPacket *packet = nullptr;
    while (isPlaying) {
        // 从队列取出AVPacket
        int ret = pkt_queue.deQueue(packet);// 音频、视频取决于AudioChannel还是VideoChannel
        if (!isPlaying) {
            break;
        }
        if (!ret) {
            continue;
        }
        // 将AVPacket作为输入数据，发送给解码器上下文
        ret = avcodec_send_packet(avCodecContext, packet);
        // 数据已经保存到avCodecContext中，释放packet
        releaseAvPacket(packet);
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
                LOG_E("其它错误, ret = %d", ret);
                continue;
            }
        }
        // 因为使用了队列，所以需要这里必须要多次开辟内存空间
        AVFrame *frame = av_frame_alloc();
        // 从解码器上下文得到AVFrame，将其作为输出数据，AVFrame中存储的是原始数据，音频是PCM，视频是YUV
        ret = avcodec_receive_frame(avCodecContext, frame);
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
            continue;
        }
        while (frame_queue.size() > QUEUE_MAX && isPlaying) {
            // 视频Frame队列超过100个，需要减缓生产
            av_usleep(10 * 1000);// 睡眠10ms
        }
        // 将AVFrame存储到队列，在AudioChannel和VideoChannel中分别解析
        frame_queue.enQueue(frame);
    }
    // 上面已经释放了，这里保险起见
    releaseAvPacket(packet);
}
