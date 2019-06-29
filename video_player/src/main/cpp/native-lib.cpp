#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window_jni.h>// ANativeWindow需要的头文件
#include <unistd.h>// usleep需要的头文件

// 因为FFmpeg是C编写的，需要使用extern关键字进行C和C++混合编译
extern "C" {
#include "libavcodec/avcodec.h"// 编解码
#include "libavformat/avformat.h"// 封装格式处理
#include "libswscale/swscale.h"// 像素处理
#include "libavutil/imgutils.h"
}

#define LOG_TAG "VideoPlayer"
// 方法别名
#define LOG_D(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define LOG_E(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

// 停止的标记位
int flag;

extern "C"
JNIEXPORT void JNICALL
Java_com_ff_video_VideoPlayer_playVideo(JNIEnv *env, jobject instance, jstring path_,
                                        jobject surface) {
    const char *path = env->GetStringUTFChars(path_, nullptr);

    // 1. 初始化网络模块
    avformat_network_init();

    // 总上下文
    AVFormatContext *pFormatContext = avformat_alloc_context();

    AVDictionary *opts = nullptr;// 字典可以理解为HashMap
    av_dict_set(&opts, "timeout", "3000000", 0);// 设置超时时间为3秒

    int ret;// 返回结果

    // 2.打开输入视频文件
    // 最后一个参数是字典，可以理解为HashMap
    ret = avformat_open_input(&pFormatContext, path, nullptr, &opts);// 需要释放 avformat_close_input
    if (ret < 0) {
        LOG_E("无法打开输入视频文件");
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 3.获取视频文件信息
    ret = avformat_find_stream_info(pFormatContext, nullptr);
    if (ret < 0) {
        LOG_E("无法获取视频文件信息");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    int video_stream_idx = -1;// 视频流的索引位置

    int i;
    // 遍历媒体流
    for (i = 0; i < pFormatContext->nb_streams; ++i) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // 找到视频流
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {// 没有视频流
        LOG_E("找不到视频流");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 4.只有知道视频的编码方式，才能够根据编码方式去找到解码器
    // 视频流的解码参数
    AVCodecParameters *pParameters = pFormatContext->streams[video_stream_idx]->codecpar;

    // 获取解码器
    AVCodec *pCodec = avcodec_find_decoder(pParameters->codec_id);
    if (pCodec == nullptr) {
        LOG_E("找不到解码器");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 解码器上下文，后面的3就是不同版本的FFmpeg函数升级后，对函数名进行了修改，作为区分
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);// 需要释放 avcodec_free_context
    if (pCodecContext == nullptr) {
        LOG_E("找不到解码器上下文");
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 将解码器的参数，复制到解码器上下文
    ret = avcodec_parameters_to_context(pCodecContext, pParameters);
    if (ret < 0) {
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 5.打开解码器
    ret = avcodec_open2(pCodecContext, pCodec, nullptr);
    if (ret < 0) {
        LOG_E("解码器无法打开");
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 初始化数据包，数据都是从AVPacket中获取
    AVPacket *pPacket = av_packet_alloc();// 内存分配，需要释放 av_packet_free
    if (pPacket == nullptr) {
        LOG_E("分配AVPacket内存失败");
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // AVFrame，解码后的音频或视频的原始数据
    AVFrame *pFrame = av_frame_alloc();// 内存分配，需要释放 av_frame_free
    if (pFrame == nullptr) {
        LOG_E("分配AVPacket内存失败");
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    // 转码上下文
    SwsContext *pSwsContext = sws_getContext(// 需要释放 sws_freeContext
            pCodecContext->width, pCodecContext->height,// 原始画面宽高
            pCodecContext->pix_fmt,// 原始画面像素格式
            pCodecContext->width, pCodecContext->height,// 目标画面宽高
            AV_PIX_FMT_RGBA,// 输出算法和选项
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (pSwsContext == nullptr) {
        LOG_E("SwsContext初始化失败");
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }

    /************************************* native绘制 start *************************************/
    // 1、获取一个关联Surface的NativeWindow窗体
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    // 2、设置缓冲区的属性（宽、高、像素格式），像素格式要和SurfaceView的像素格式一致
    ANativeWindow_setBuffersGeometry(nativeWindow,
                                     pCodecContext->width,
                                     pCodecContext->height,
                                     WINDOW_FORMAT_RGBA_8888);
    // 绘制时的缓冲区，window的缓冲区
    ANativeWindow_Buffer outBuffer;

    // 保存图像通道的地址。如果是RGBA，分别指向R，G，B，A的内存地址；如果是RGB，第四个指针保留不用
    uint8_t *dst_data[4];// 指针数组，数组中存有4个uint8_t类型的指针
    // 保存图像每个通道的内存对齐的步长，即一行的对齐内存的宽度，此值大小等于图像宽度。
    int dst_linesize[4];// int数组，数组中存有4个int类型
    // 分配一个宽高为输入视频宽高，像素格式为RGBA8888的图像，并初始化指针和线条。
    ret = av_image_alloc(dst_data,// 需要释放 av_freep
                         dst_linesize,
                         pCodecContext->width,// 要申请内存的图像宽度
                         pCodecContext->height,// 要申请内存的图像高度
                         AV_PIX_FMT_RGBA,// 要申请内存的图像的像素格式
                         1);// 用于内存对齐的值

    if (ret < 0) {
        LOG_E("分配内存失败");
        av_freep(&dst_data[0]);
        ANativeWindow_release(nativeWindow);
        sws_freeContext(pSwsContext);
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        avcodec_free_context(&pCodecContext);
        avformat_close_input(&pFormatContext);
        env->ReleaseStringUTFChars(path_, path);
        return;
    }
    /************************************* native绘制 end ***************************************/

    // 为了简单模拟，写死每一帧的间隔时间，单位是微秒
    const useconds_t sleepTime = 1000 * 20;
    flag = 1;

    // 6.一帧一帧的读取压缩视频数据AVPacket
    while (av_read_frame(pFormatContext, pPacket) >= 0 && flag) {// 返回下一帧
        if (pPacket->stream_index == video_stream_idx) {// 判断视频帧
            // 7.解码一帧视频压缩数据，得到视频像素数据，AVPacket（压缩数据）->AVFrame（未压缩数据）

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
                    // pFrame中就得到了一帧视频数据，存储着解码后的YUV数据
                    // 后面要做的就是将YUV数据，渲染到SurfaceView中，这里就需要ANativeWindow了

                    if (ANativeWindow_lock(nativeWindow, &outBuffer, nullptr) < 0) {
                        break;
                    }

                    /**
                     * 将YUV输入数据，转换为输出数据，将信息存储在容器dst_data和dst_linesize中
                     * 2 6 输入、输出数据
                     * 3 7 输入、输出画面一行的数据的大小 AVFrame 转换是一行一行转换的
                     * 4 输入数据第一列要转码的位置 从0开始
                     * 5 输入画面的高度
                     */
                    sws_scale(pSwsContext, pFrame->data,
                              pFrame->linesize, 0, pFrame->height,
                              dst_data, dst_linesize);

                    auto *firstWindow = static_cast<uint8_t *>(outBuffer.bits);
                    // 输入源数据（RGBA）
                    uint8_t *src_data = dst_data[0];
                    // 计算一行有多少字节，stride是一行像素，需要乘以4，因为RGBA占4位
                    int desStride = outBuffer.stride * 4;
                    int srcStride = dst_linesize[0];

                    for (i = 0; i < outBuffer.height; ++i) {
                        // 内存拷贝，一行一行的进行渲染效率高
                        // 整块渲染虽然更快，但窗体大小和实际大小不一致，会出现错位
                        memcpy(firstWindow + i * desStride, src_data + i * srcStride,
                               static_cast<size_t>(desStride));
                    }

                    ANativeWindow_unlockAndPost(nativeWindow);
                    usleep(sleepTime);// 避免执行太快，有的帧率很高的视频，不睡眠可能很慢
                }
            } while (false);// while (ret >= 0);// 这里循环的目的是：怕有未消费数据包，但是没有出现过
        }
        // 注意av_packet_alloc()这种方式初始化的，不是在这里释放
        // av_packet_free(&pPacket);
    }
    // 释放
    av_freep(&dst_data[0]);
    // 释放ANativeWindow
    ANativeWindow_release(nativeWindow);
    // 释放SwsContext
    sws_freeContext(pSwsContext);
    // 释放AVFrame
    av_frame_free(&pFrame);
    // 释放AVPacket
    av_packet_free(&pPacket);
    // 释放AVCodecContext
    avcodec_free_context(&pCodecContext);
    // 释放AVFormatContext
    avformat_close_input(&pFormatContext);
    // 释放字符串
    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ff_video_VideoPlayer_stopVideo(JNIEnv *env, jobject instance) {
    flag = 0;
}