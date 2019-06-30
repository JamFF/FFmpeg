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
        (*bq)->Enqueue(bq, audioChannel->buffer, data_len);
    }
}

AudioChannel::AudioChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext)
        : BaseChannel(id, javaCallHelper, avCodecContext) {
    out_sample_rate = 44100;// 采样率
    // 根据布局获取声道数
    out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);// 双声道
    out_sample_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);// 16位，2字节
    // CD音频标准，44100，双声道，16bit
    buffer = static_cast<uint8_t *>(malloc(out_sample_rate * out_sample_size * out_channels));
}

void AudioChannel::start() {
    swr_ctx = swr_alloc_set_opts(nullptr,
                                 AV_CH_LAYOUT_STEREO,
                                 AV_SAMPLE_FMT_S16,
                                 out_sample_rate,
                                 avCodecContext->channel_layout,
                                 avCodecContext->sample_fmt,
                                 avCodecContext->sample_rate,
                                 0, nullptr);

    swr_init(swr_ctx);// 初始化转换器上下文

    pkt_queue.setWork(1);
    frame_queue.setWork(1);
    isPlaying = true;// TODO 播放完成需要设置false
    // 开启音频解码线程
    pthread_create(&pid_audio_decode, nullptr, decodeAudio, this);
    // 开启初始化OpenSL ES线程
    pthread_create(&pid_audio_play, nullptr, audioPlay, this);
}

void AudioChannel::stop() {

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
    AVFrame *frame = nullptr;
    int ret;
    int64_t dst_nb_samples;
    unsigned int nb;
    unsigned int data_size = 0;
    if (isPlaying) {
        // 从队列中取出AVFrame，存储的是PCM原始数据
        ret = frame_queue.deQueue(frame);
        if (!ret) {
            LOG_E("getPCM deQueue ret = %d", ret);
        } else {
            dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
                    out_sample_rate,
                    frame->sample_rate,
                    AV_ROUND_UP);
            // 转换，返回值为转换后的sample个数
            nb = static_cast<unsigned int>(swr_convert(swr_ctx,// 重采样上下文
                                                       &buffer,// 输出缓冲区
                                                       static_cast<int>(dst_nb_samples),// 每个通道采样中可用于输出的空间量
                                                       (const uint8_t **) frame->data,// 输入缓冲区
                                                       frame->nb_samples));// 一个通道中可用的输入采样数量
            // 转换后，数据大小 44110*2*2
            data_size = nb * out_channels * out_sample_size;
            // 0.05s
//        clock = frame->pts * av_q2d(time_base);
        }
    }
    releaseAvFrame(frame);
    return data_size;
}
