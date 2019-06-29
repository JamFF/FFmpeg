//
// Created by FF on 2019-06-26.
//

#include "JavaCallHelper.h"

JavaCallHelper::JavaCallHelper(JavaVM *_javaVM, JNIEnv *_env, jobject &_jobj) : javaVM(_javaVM),
                                                                                env(_env) {
    // 可以使用 ': javaVM(_javaVM), env(_env)' 方式代替下面赋值
    // javaVM = _javaVM;
    // env = _env;

    // jobj需要在其它方法中使用，所以创建全局引用，延长生命周期
    jobj = env->NewGlobalRef(_jobj);// TODO 注意适用释放，env->DeleteGlobalRef(jobj);
    jclass jclazz = env->GetObjectClass(jobj);

    // jmid_error  ArtMethod
    jmid_error = env->GetMethodID(jclazz, "onError", "(I)V");
    jmid_prepare = env->GetMethodID(jclazz, "onPrepare", "()V");
    jmid_progress = env->GetMethodID(jclazz, "onProgress", "(I)V");
}

JavaCallHelper::~JavaCallHelper() {

}

void JavaCallHelper::onError(int thread, int code) {
    if (thread == THREAD_CHILD) {
        JNIEnv *jniEnv;// 子线程的JNIEnv
        if (javaVM->AttachCurrentThread(&jniEnv, nullptr) != JNI_OK) {// 绑定线程，得到子线程JNIEnv
            return;
        }
        jniEnv->CallVoidMethod(jobj, jmid_error, code);
        javaVM->DetachCurrentThread();
    } else if (thread == THREAD_MAIN) {
        env->CallVoidMethod(jobj, jmid_error, code);
    }
}

void JavaCallHelper::onPrepare(int thread) {
    if (thread == THREAD_CHILD) {
        JNIEnv *jniEnv;
        if (javaVM->AttachCurrentThread(&jniEnv, nullptr) != JNI_OK) {
            return;
        }
        jniEnv->CallVoidMethod(jobj, jmid_prepare);
        javaVM->DetachCurrentThread();
    } else if (thread == THREAD_MAIN) {
        env->CallVoidMethod(jobj, jmid_prepare);
    }
}

void JavaCallHelper::onProgress(int thread, int progress) {
    if (thread == THREAD_CHILD) {
        JNIEnv *jniEnv;
        if (javaVM->AttachCurrentThread(&jniEnv, nullptr) != JNI_OK) {
            return;
        }
        jniEnv->CallVoidMethod(jobj, jmid_progress, progress);
        javaVM->DetachCurrentThread();
    } else if (thread == THREAD_MAIN) {
        env->CallVoidMethod(jobj, jmid_progress, progress);
    }
}
