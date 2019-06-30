//
// Created by FF on 2019-06-26.
//

#include "AudioChannel.h"

void *decodeAudio(void *args) {
    auto *pAudioChannel = static_cast<AudioChannel *>(args);
    pAudioChannel->decodePacket();
    return nullptr;
}

void *audioPlay(void *args) {
    auto *pAudioChannel = static_cast<AudioChannel *>(args);
    pAudioChannel->initOpenSL();
    return nullptr;
}

/**
 * 手动调用第一次后，之后会不断被回调
 * @param bq
 * @param context
 */
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    auto audioChannel = static_cast<AudioChannel *>(context);
    unsigned int data_len = audioChannel->getPCM();
    if (data_len > 0) {
        // 添加到缓冲队列
        (*bq)->Enqueue(bq, audioChannel->out_buffer, data_len);
    }
}

AudioChannel::AudioChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext,
                           AVRational time_base)
        : BaseChannel(id, javaCallHelper, avCodecContext, time_base) {
    // 设置音频输出格式，不管什么样的PCM格式，都重采样为统一的输出格式，CD音频标准，44100，双声道，16bit
    // 采样率
    out_sample_rate = 44100;
    // 根据布局获取声道数，双声道
    out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    // 16bit，2字节
    out_sample_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    // 输出缓冲区
    out_buffer = static_cast<uint8_t *>(malloc(out_sample_rate * out_sample_size * out_channels));
}

void AudioChannel::start() {
    // 创建重采样上下文，并设置参数
    swr_ctx = swr_alloc_set_opts(nullptr,// 如果为NULL则创建SwrContext，否则对传入SwrContext进行参数设置
                                 AV_CH_LAYOUT_STEREO,// 输出的声道格式，AV_CH_LAYOUT_*
                                 AV_SAMPLE_FMT_S16,// 输出采样位数
                                 out_sample_rate,// 输出采样率
                                 avCodecContext->channel_layout,// 输入的声道格式
                                 avCodecContext->sample_fmt,// 输入采样位数
                                 avCodecContext->sample_rate,// 输入采样率
                                 0, nullptr);

    swr_init(swr_ctx);// 初始化重采样上下文

    pkt_queue.setWork(1);
    frame_queue.setWork(1);
    isPlaying = true;// TODO 播放完成需要设置false？
    // 开启音频解码线程
    pthread_create(&pid_audio_decode, nullptr, decodeAudio, this);
    // 开启初始化OpenSL ES线程
    pthread_create(&pid_audio_play, nullptr, audioPlay, this);
}

void AudioChannel::stop() {
    // TODO av_freep(&out_buffer);注意释放
}

/**
 * 初始化OpenSL，运行在子线程
 */
void AudioChannel::initOpenSL() {
    // 音频引擎
    SLEngineItf engineInterface = nullptr;
    // 音频对象
    SLObjectItf engineObject = nullptr;
    // 混音器
    SLObjectItf outputMixObject = nullptr;

    // 播放器
    SLObjectItf bqPlayerObject = nullptr;
    // Player接口
    SLPlayItf bqPlayerInterface = nullptr;
    // 缓冲队列
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = nullptr;

    // 1. -------------------------初始化播放引擎-------------------------
    SLresult result;
    result = slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    if (SL_RESULT_SUCCESS != result) {
        // 失败，没有音频权限，没有喇叭之类的原因
        return;
    }
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }
    // 音频接口 相当于SurfaceHolder
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineInterface);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }

    // 2. -------------------------初始化混音器-------------------------
    result = (*engineInterface)->CreateOutputMix(engineInterface, &outputMixObject, 0, nullptr,
                                                 nullptr);
    // 初始化混音器outputMixObject
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }

    // 3. -------------------------初始化播放器-------------------------
    // 播放缓冲队列
    SLDataLocator_AndroidSimpleBufferQueue android_queue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                            2};// 双声道（立体声）
    // PCM数据格式
    SLDataFormat_PCM pcm = {SL_DATAFORMAT_PCM,// 播放pcm格式的数据
                            2,// 双声道（立体声）
                            SL_SAMPLINGRATE_44_1, // 采样率 44100hz的频率
                            SL_PCMSAMPLEFORMAT_FIXED_16,// 采样位数 16位
                            SL_PCMSAMPLEFORMAT_FIXED_16,// 和采样位数一致就行
                            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,// 立体声（前左前右）
                            SL_BYTEORDER_LITTLEENDIAN};// 小端模式

    // 播放器参数，包含播放缓冲队列和播放数据格式
    SLDataSource slDataSource = {&android_queue, &pcm};

    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};

    // 播放缓冲区
    SLDataSink audioSnk = {&outputMix, nullptr};

    // 播放队列ID
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};

    // 采用内置的播放队列
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};

    (*engineInterface)->CreateAudioPlayer(engineInterface,// 音频引擎
                                          &bqPlayerObject,// 播放器
                                          &slDataSource,// 播放器参数
                                          &audioSnk,// 播放缓冲区
                                          1,// 播放接口回调个数
                                          ids,// 设置播放队列ID
                                          req);// 是否采用内置的播放队列

    // 初始化播放器
    (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);

    // 4. -------------------------设置缓冲队列和回调函数-------------------------
    // 获取Player接口
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerInterface);
    // 获得缓冲队列
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);
    // 设置回调函数
    (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this);
    // 设置播放状态
    (*bqPlayerInterface)->SetPlayState(bqPlayerInterface, SL_PLAYSTATE_PLAYING);
    // 启动回调函数
    LOG_D("手动调用播放 packet:%d", this->pkt_queue.size());
    bqPlayerCallback(bqPlayerBufferQueue, this);
}

/**
 * 获取PCM数据后重采样
 * @return
 */
unsigned int AudioChannel::getPCM() {
    AVFrame *pAVFrame = nullptr;
    int ret;
    int64_t dst_nb_samples;
    unsigned int nb;
    unsigned int data_size = 0;
    if (isPlaying) {
        // 从队列中取出AVFrame，存储的是PCM原始数据
        ret = frame_queue.deQueue(pAVFrame);
        if (!ret) {
            LOG_E("getPCM deQueue ret = %d", ret);
        } else {
            dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, pAVFrame->sample_rate) + pAVFrame->nb_samples,
                    out_sample_rate,
                    pAVFrame->sample_rate,
                    AV_ROUND_UP);
            // 开启重采样，返回每个通道转换后的sample数量，发生错误的时候返回负数
            nb = static_cast<unsigned int>(swr_convert(swr_ctx,// 重采样上下文
                                                       &out_buffer,// 输出缓冲区
                                                       static_cast<int>(dst_nb_samples),// 每个通道采样中可用于输出PCM数据的sample数量
                                                       (const uint8_t **) pAVFrame->data,// 输入缓冲区
                                                       pAVFrame->nb_samples));// 每个通道可用于输入PCM数据的sample数量
            if (nb > 0) {
                // 转换后，数据大小 44110*2*2
                data_size = nb * out_channels * out_sample_size;
            } else {
                LOG_E("swr_convert negative value on error");
            }

            // 由于视频丢一帧感受不到，但是音频不一样，所以计算音频间隔，在视频播放中与音频进行对齐
            // pts是以time_base为单位的呈现时间戳（应向用户显示帧的时间）
            // time_base是表示帧时间戳的基本时间单位（以秒为单位）
            // 通过这两个参数，可以计算得以秒为单位的呈现时间戳
            clock = pAVFrame->pts * av_q2d(time_base);
            // TODO 有博客说音频使用pts，视频使用best_effort_timestamp，目前没有发现不一致的情况
            /*LOG_D("audio best_effort_timestamp = %lld, pts = %lld", pAVFrame->best_effort_timestamp,
                  pAVFrame->pts);*/
        }
    }
    releaseAvFrame(pAVFrame);
    return data_size;
}
