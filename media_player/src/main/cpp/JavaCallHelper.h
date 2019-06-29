//
// Created by FF on 2019-06-26.
//

#ifndef FFMPEG_JAVACALLHELPER_H
#define FFMPEG_JAVACALLHELPER_H

#include <jni.h>
#include "macro.h"

class JavaCallHelper {
public:

    JavaCallHelper(JavaVM *_javaVM, JNIEnv *_env, jobject &_jobj);

    ~JavaCallHelper();

    void onError(int thread, int code);

    void onPrepare(int thread);

    void onProgress(int thread, int progress);

private:
    JavaVM *javaVM;// 由于需要子线程中，反射调用Java方法，
    JNIEnv *env;// 主线程的JNIEnv
    jobject jobj;
    jmethodID jmid_prepare;
    jmethodID jmid_error;
    jmethodID jmid_progress;
};


#endif //FFMPEG_JAVACALLHELPER_H
