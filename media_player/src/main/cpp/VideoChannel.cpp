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

/**
 * 丢弃AVPacket队列中的非关键帧
 * @param q
 */
void dropPacket(queue<AVPacket *> &q) {
    while (!q.empty()) {
        AVPacket *pkt = q.front();
        if (pkt->flags != AV_PKT_FLAG_KEY) {
            // 压缩数据，存在关键帧，需要过滤，如果丢弃了I帧，会导致B帧、P帧不能解码
            q.pop();
            BaseChannel::releaseAvPacket(pkt);
            LOG_E("dropPacket size = %ld", q.size());
        } else {
            // 直到下一个关键帧停止
            break;
        }
    }
}

/**
 * 丢弃AVFrame队列数据（YUV数据）
 * @param q
 */
void dropFrame(queue<AVFrame *> &q) {
    if (!q.empty()) {
        AVFrame *frame = q.front();
        // 相比丢弃AVPacket队列，有两个优点
        // 1. 解码后的YUV数据，不存在关键帧的问题
        // 2. AVFrame的队列数据直接关联ANativeWindow，如果丢弃AVPacket，AVFrame队列依然是满的，不能快速解决问题
        q.pop();
        BaseChannel::releaseAvFrame(frame);
        LOG_E("dropFrame size = %ld", q.size());
    }
}

VideoChannel::VideoChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext,
                           AVRational time_base)
        : BaseChannel(id, javaCallHelper, avCodecContext, time_base) {

    frame_queue.setReleaseHandle(releaseAvFrame);// 设置丢帧策略
    frame_queue.setSyncHandle(dropFrame);// 设置丢帧方法

    pkt_queue.setReleaseHandle(releaseAvPacket);// 设置丢帧策略
    pkt_queue.setSyncHandle(dropPacket);// 设置丢帧方法
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

    AVFrame *pAVFrame = nullptr;

    // 根据帧率，计算每帧的延迟时间，单位是秒，固定的值
    double frame_delays = 1 / fps;

    while (isPlaying) {
        // 从队列中取出AVFrame，存储的是YUV原始数据
        ret = frame_queue.deQueue(pAVFrame);
        if (!isPlaying) {
            break;
        }
        if (!ret) {
            LOG_E("synchronizeFrame deQueue ret = %d", ret);
            continue;
        }

        // TODO 有博客说视频使用best_effort_timestamp，目前没有发现不一致的情况
        /*LOG_D("video best_effort_timestamp = %lld, pts = %lld", pAVFrame->best_effort_timestamp,
              pAVFrame->pts);*/

        // 解决音视频同步，需要计算音视频时间差diff，调整视频播放，向音频对齐
        // PTS是播放时间是在编码时确定的，没有考虑解码时间
        clock = pAVFrame->pts * av_q2d(time_base);// 视频的播放时间
        double audioClock = pAudioChannel->clock;// 音频的播放时间
        double diff = clock - audioClock;// 为正，视频超前，为负，音频超前

        // 将解码耗时计算进去，配置差的手机，解码耗时更长
        // 解码时，extra_delay表示图片必须延迟多少，音频中不需要关注解码时间
        // 因为音频解码是由OpenSL ES内部循环调用的，并不是我们自己的while循环
        double extra_delay = pAVFrame->repeat_pict / (2 * fps);// TODO 一直为0，不理解
        if (ALL_LOG) {
            LOG_D("repeat_pict = %d, extra_delay = %lf", pAVFrame->repeat_pict, extra_delay);
        }

        // 真正需要延迟的时间，单位是秒，每帧视频延时时间，再加上解码延迟时间
        double delay = frame_delays + extra_delay;
        if (ALL_LOG) {
            LOG_D("delay = %lf ms", delay * 1000);
        }
        if (diff > 0) {// 视频超前
            // 以delay区分的目的是，避免一次睡眠时间太久，用户感知
            if (diff < delay) {
                // 视频超前小于延迟时间，按照标准方式睡眠处理，延迟时间+音视频时间差
                if (ALL_LOG) {
                    LOG_D("视频超前 %dms，视频增加延迟", static_cast<int>(diff * 1000));
                }
                av_usleep(static_cast<unsigned int>((delay + diff) * 1000000));// 换算微妙
            } else {
                // 视频超前大于延迟时间，2倍的延迟时间睡眠处理，更加平滑
                LOG_D("视频超前 %dms，视频2倍延迟", static_cast<int>(diff * 1000));
                av_usleep(static_cast<unsigned int>(delay * 2 * 1000000));// 换算微妙
            }
        } else if (diff < 0) {// 音频超前
            if (delay + diff > 0) {
                // 音频超前小于延迟时间，减少视频延迟
                if (ALL_LOG) {
                    LOG_D("音频超前 %dms，视频减少延迟", static_cast<int>(fabs(diff) * 1000));
                }
                // av_usleep(static_cast<unsigned int>((delay + diff) * 1000000));// 换算微妙
            } else {
                // 音频超前大于延迟时间，即使不延迟，视频播放还是落后音频
                // 采用不睡眠，并且丢帧的方式，进行追赶
                LOG_D("音频超前 %dms，视频丢帧", static_cast<int>(fabs(diff) * 1000));
                releaseAvFrame(pAVFrame);
                frame_queue.sync();
                if (fabs(diff) > 0.1) {
                    // 音频超前大于100ms，AVPacket也需要进行丢帧
                    pkt_queue.sync();
                }
                continue;// 进入下一次循环，不需要发送给ANativeWindow绘制来
            }
        } else {
            if (ALL_LOG) {
                LOG_D("音视频同步");
            }
            // 音视频同步，只处理延迟时间
            av_usleep(static_cast<unsigned int>(frame_delays * 1000000));
        }

        /**
         * 将原始数据YUV I420（NV21），转码为RGBA8888
         * 将播放信息存储在容器dst_data和dst_linesize中
         * 2 6 输入、输出数据
         * 3 7 输入、输出画面一行的数据的大小 AVFrame 转换是一行一行转换的
         * 4 输入数据第一列要转码的位置 从0开始
         * 5 输入画面的高度
         */
        sws_scale(sws_ctx, pAVFrame->data,
                  pAVFrame->linesize, 0, pAVFrame->height,
                  dst_data, dst_linesize);

        // 回调给native-lib，让ANativeWindow去绘制
        renderFrame(dst_data[0], dst_linesize[0], avCodecContext->width, avCodecContext->height);

        // 拿到了RGBA数据，就可以释放原始数据
        releaseAvFrame(pAVFrame);
    }
    releaseAvFrame(pAVFrame);
    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);
    isPlaying = false;
}

void VideoChannel::setRenderCallback(RenderFrame renderFrame) {
    this->renderFrame = renderFrame;
}

void VideoChannel::setFPS(double fps) {
    this->fps = fps;
}
