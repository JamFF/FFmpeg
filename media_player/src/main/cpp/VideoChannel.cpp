//
// Created by FF on 2019-06-26.
//

#include "VideoChannel.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void *decodeVideo(void *args) {
    auto *pVideoChannel = static_cast<VideoChannel *>(args);
    pVideoChannel->decodePacket();
    return nullptr;
}

void *synchronize(void *args) {
    auto *pVideoChannel = static_cast<VideoChannel *>(args);
    pVideoChannel->synchronizeFrame();
    return nullptr;
}

VideoChannel::VideoChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext)
        : BaseChannel(id, javaCallHelper, avCodecContext) {

}

void VideoChannel::start() {
    pkt_queue.setWork(1);
    frame_queue.setWork(1);
    isPlaying = true;
    // 开启视频解码线程
    pthread_create(&pid_video_decode, nullptr, decodeVideo, this);
    // 开启视频播放线程
    pthread_create(&pid_synchronize, nullptr, synchronize, this);
}

void VideoChannel::stop() {

}

/**
 * 封装数据并播放，运行在子线程
 */
void VideoChannel::synchronizeFrame() {
    // 转码上下文
    SwsContext *sws_ctx = sws_getContext(// 需要释放 sws_freeContext
            avCodecContext->width, avCodecContext->height,// 原始画面宽高
            avCodecContext->pix_fmt,// 原始画面像素格式
            avCodecContext->width, avCodecContext->height,// 目标画面宽高
            AV_PIX_FMT_RGBA,// 输出算法和选项，RGBA8888，因为SurfaceView支持
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (sws_ctx == nullptr) {
        LOG_E("SwsContext初始化失败");
        isPlaying = false;
        return;
    }

    // 保存图像通道的地址。如果是RGBA，分别指向R，G，B，A的内存地址；如果是RGB，第四个指针保留不用
    uint8_t *dst_data[4];// 指针数组，数组中存有4个uint8_t类型的指针
    // 保存图像每个通道的内存对齐的步长，即一行的对齐内存的宽度，此值大小等于图像宽度。
    int dst_linesize[4];// int数组，数组中存有4个int类型
    // 分配一个宽高为输入视频宽高，像素格式为RGBA8888的图像，并初始化指针和线条。
    int ret = av_image_alloc(dst_data,// 需要释放 av_freep
                             dst_linesize,
                             avCodecContext->width,// 要申请内存的图像宽度
                             avCodecContext->height,// 要申请内存的图像高度
                             AV_PIX_FMT_RGBA,// 要申请内存的图像的像素格式
                             1);// 用于内存对齐的值

    // dst_linesize只有第一个元素有值，就是宽度，比如视频宽*高为1904*784，那么第一个元素值为1904*4=7616
    LOG_D("dst_linesize = %d, %d, %d, %d", dst_linesize[0],
          dst_linesize[1], dst_linesize[2], dst_linesize[3]);

    if (ret < 0) {
        LOG_E("分配内存失败");
        av_freep(&dst_data[0]);
        sws_freeContext(sws_ctx);
        isPlaying = false;
        return;
    }

    AVFrame *frame = nullptr;
    while (isPlaying) {
        // 从队列中取出AVFrame，存储的是YUV原始数据
        ret = frame_queue.deQueue(frame);
        if (!isPlaying) {
            break;
        }
        if (!ret) {
            LOG_E("synchronizeFrame deQueue ret = %d", ret);
            continue;
        }

        /**
         * 将原始数据YUV I420（NV21），转码为RGBA8888
         * 将播放信息存储在容器dst_data和dst_linesize中
         * 2 6 输入、输出数据
         * 3 7 输入、输出画面一行的数据的大小 AVFrame 转换是一行一行转换的
         * 4 输入数据第一列要转码的位置 从0开始
         * 5 输入画面的高度
         */
        sws_scale(sws_ctx, frame->data,
                  frame->linesize, 0, frame->height,
                  dst_data, dst_linesize);

        // 回调给 native-lib 去绘制
        renderFrame(dst_data[0], dst_linesize[0], avCodecContext->width, avCodecContext->height);
        // FIXME 由于还没做音视频同步，延时16ms
        av_usleep(16 * 1000);
        // 拿到了RGBA数据，就可以释放原始数据
        releaseAvFrame(frame);
    }
    releaseAvFrame(frame);
    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);
    isPlaying = false;
}

void VideoChannel::setRenderCallback(RenderFrame renderFrame) {
    this->renderFrame = renderFrame;
}
