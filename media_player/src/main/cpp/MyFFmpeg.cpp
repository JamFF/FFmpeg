//
// Created by FF on 2019-06-25.
//

#include "MyFFmpeg.h"

/**
 * 被子线程调用的初始化FFmpeg，需要定义为全局函数
 */
void *prepareThread(void *args) {
    auto *pFFmpeg = static_cast<MyFFmpeg *>(args);
    pFFmpeg->prepareFFmpeg();
    return nullptr;
}

/**
 * 被子线程调用的获取数据包，需要定义为全局函数
 */
void *packetThread(void *args) {
    auto *pFFmpeg = static_cast<MyFFmpeg *>(args);
    pFFmpeg->getPacket();
    return nullptr;
}

MyFFmpeg::MyFFmpeg(JavaCallHelper *javaCallHelper, const char *dataSource) {
    pJavaCallHelper = javaCallHelper;
    // strlen函数返回字符串长度，计算规则是直到遇到字符串中末尾的'\0'
    url = new char[strlen(dataSource) + 1];// 这里 +1 也是因为最后以为是'\0'
    strcpy(url, dataSource);
}

MyFFmpeg::~MyFFmpeg() {

}

/**
 * 子线程初始化FFmpeg
 */
void MyFFmpeg::prepare() {
    // 参数1：线程id，pthread_t型指针
    // 参数2：线程的属性，nullptr默认属性
    // 参数3：线程创建之后执行的函数，函数指针，可以写&prepareFFmpeg，由于函数名就是函数指针，所以可以省略&
    // 参数4：prepareFFmpeg函数接受的参数，void型指针
    pthread_create(&pid_prepare, nullptr, prepareThread, this);
}

/**
 * 初始化Fmpeg，运行在子线程
 */
void MyFFmpeg::prepareFFmpeg() {
    // 1.初始化网络模块
    avformat_network_init();

    // 总上下文，包含了视频、音频的各种信息
    pFormatContext = avformat_alloc_context();

    AVDictionary *opts = nullptr;// 字典可以理解为HashMap
    av_dict_set(&opts, "timeout", "3000000", 0);// 设置超时时间为3秒

    int ret;// 返回结果

    // 2.打开输入视频文件
    // 第三个参数是输入文件的封装格式，可以强制指定AVFormatContext中AVInputFormat的。
    // 一般设置为nullptr，可以自动检测AVInputFormat
    ret = avformat_open_input(&pFormatContext, url, nullptr, &opts);// 需要释放 avformat_close_input
    if (ret < 0) {
        LOG_E("无法打开输入视频文件");
        if (pJavaCallHelper != nullptr) {
            pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_OPEN_URL);
        }
        return;
    }
    // 3.获取视频文件信息
    if (avformat_find_stream_info(pFormatContext, nullptr) < 0) {
        if (pJavaCallHelper != nullptr) {
            pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_FIND_STREAMS);
        }
        return;
    }

    for (int i = 0; i < pFormatContext->nb_streams; ++i) {
        AVCodecParameters *codecpar = pFormatContext->streams[i]->codecpar;
        // 找到解码器
        AVCodec *dec = avcodec_find_decoder(codecpar->codec_id);
        if (!dec) {
            if (pJavaCallHelper != nullptr) {
                pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_FIND_DECODER_FAIL);
            }
            return;
        }
        // 创建解码器上下文
        AVCodecContext *codecContext = avcodec_alloc_context3(dec);
        if (!codecContext) {
            if (pJavaCallHelper != nullptr) {
                pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_ALLOC_CODEC_CONTEXT_FAIL);
            }
            return;
        }
        // 复制参数
        if (avcodec_parameters_to_context(codecContext, codecpar) < 0) {
            if (pJavaCallHelper != nullptr) {
                pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_CODEC_CONTEXT_PARAMETERS_FAIL);
            }
            return;
        }
        // 打开解码器
        if (avcodec_open2(codecContext, dec, nullptr) < 0) {
            if (pJavaCallHelper != nullptr) {
                pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_OPEN_DECODER_FAIL);
            }
            return;
        }
        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {// 音频
            pAudioChannel = new AudioChannel(i, pJavaCallHelper, codecContext);
        } else if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {// 视频
            pVideoChannel = new VideoChannel(i, pJavaCallHelper, codecContext);
            pVideoChannel->setRenderCallback(renderFrame);
        }
    }

    // 音视频都没有
    if (pAudioChannel == nullptr && pVideoChannel == nullptr) {
        if (pJavaCallHelper != nullptr) {
            pJavaCallHelper->onError(THREAD_CHILD, FFMPEG_NOMEDIA);
        }
        return;
    }
    if (pJavaCallHelper != nullptr) {
        pJavaCallHelper->onPrepare(THREAD_CHILD);
    }
}

void MyFFmpeg::start() {
    isPlaying = true;
    if (pAudioChannel != nullptr) {
        // TODO 音频时需要打开
        pAudioChannel->start();
    }
    if (pVideoChannel != nullptr) {
        pVideoChannel->start();
    }
    if (pAudioChannel != nullptr || pVideoChannel != nullptr) {
        // 开启获取packet线程
        pthread_create(&pid_decode, nullptr, packetThread, this);
    }
}

void MyFFmpeg::getPacket() {
    int ret = 0;
    while (isPlaying) {
        if ((pAudioChannel != nullptr && pAudioChannel->pkt_queue.size() > QUEUE_MAX)
            || (pVideoChannel != nullptr && pVideoChannel->pkt_queue.size() > QUEUE_MAX)) {
//            LOG_D("Audio queue size = %d", pAudioChannel->pkt_queue.size());
//            LOG_D("Video queue size = %d", pVideoChannel->pkt_queue.size());
            // 音频或视频Packet队列超过100个，需要减缓生产
            av_usleep(10 * 1000);// 睡眠10ms
            continue;
        }

        // 因为使用了队列，所以需要这里必须要多次开辟内存空间
        AVPacket *packet = av_packet_alloc();// 需要使用av_packet_free释放，在BaseChannel中
        // 从媒体中读取音频包、视频包
        ret = av_read_frame(pFormatContext, packet);

        if (ret == 0) {
            // 将数据包加入队列
            if (pAudioChannel != nullptr && packet->stream_index == pAudioChannel->channelId) {
                // TODO 音频时需要打开
                pAudioChannel->pkt_queue.enQueue(packet);
            } else if (pVideoChannel != nullptr &&
                       packet->stream_index == pVideoChannel->channelId) {
                pVideoChannel->pkt_queue.enQueue(packet);
            }
        } else if (ret == AVERROR_EOF) {
            // 读取完毕 但是不一定播放完毕
            if (pVideoChannel->pkt_queue.empty() && pVideoChannel->frame_queue.empty()
                && pAudioChannel->pkt_queue.empty() && pAudioChannel->frame_queue.empty()) {
                LOG_D("播放完毕。。。");
                // TODO 需要停止队列
                break;
            }
            // 因为seek的存在，就算读取完毕，依然要循环，去执行av_read_frame(否则seek了没用)
        } else {
            break;
        }
    }

    isPlaying = false;
    pAudioChannel->stop();
    pVideoChannel->stop();
}

void MyFFmpeg::setRenderCallback(RenderFrame renderFrame) {
    this->renderFrame = renderFrame;
}
