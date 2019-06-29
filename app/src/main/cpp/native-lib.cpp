#include <jni.h>
#include <string>

// 因为FFmpeg是C编写的，需要使用extern关键字进行C和C++混合编译
extern "C" {
#include "libavcodec/avcodec.h"
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ff_ffmpeg_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    return env->NewStringUTF(av_version_info());
}