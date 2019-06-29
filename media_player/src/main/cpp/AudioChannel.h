//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_AUDIOCHANNEL_H
#define FFMPEG_AUDIOCHANNEL_H

#include "BaseChannel.h"

class AudioChannel : public BaseChannel {

public:
    AudioChannel(int id, JavaCallHelper *javaCallHelper, AVCodecContext *avCodecContext);

    void start();

    void stop();
};


#endif //FFMPEG_AUDIOCHANNEL_H
