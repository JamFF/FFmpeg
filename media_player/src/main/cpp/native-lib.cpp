#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include "MyFFmpeg.h"
#include "JavaCallHelper.h"

// 因为FFmpeg是C编写的，需要使用extern关键字进行C和C++混合编译
extern "C" {
#include "libavcodec/avcodec.h"
}

ANativeWindow *pWindow;
MyFFmpeg *pFFmpeg;
JavaCallHelper *pJavaCallHelper;
JavaVM *javaVM;// 子线程需要通过JavaVM，才能获取到当前线程的env，然后才能反射

/**
 * 渲染一帧视频数据
 * @param data 输入源数据
 * @param linesize
 * @param w 宽
 * @param h 高
 */
void renderFrame(uint8_t *data, int linesize, int w, int h) {

    // 设置缓冲区的属性（宽、高、像素格式），像素格式要和SurfaceView的像素格式一致
    ANativeWindow_setBuffersGeometry(pWindow, w, h, WINDOW_FORMAT_RGBA_8888);

    // 绘制时的缓冲区，window的缓冲区
    ANativeWindow_Buffer window_buffer;
    if (ANativeWindow_lock(pWindow, &window_buffer, nullptr) < 0) {
        ANativeWindow_release(pWindow);
        pWindow = nullptr;
        return;
    }

    auto *window_data = static_cast<uint8_t *>(window_buffer.bits);
    // 计算一行有多少字节，stride是一行像素，需要乘以4，因为RGBA占4位
    int window_linesize = window_buffer.stride * 4;
    // 输入源数据（RGBA）
    uint8_t *src_data = data;
    for (int i = 0; i < window_buffer.height; ++i) {
        // 内存拷贝，一行一行的进行渲染效率高
        // 整块渲染虽然更快，但窗体大小和实际大小不一致，会出现错位
        memcpy(window_data + i * window_linesize, src_data + i * linesize,
               static_cast<size_t>(window_linesize));
    }
    ANativeWindow_unlockAndPost(pWindow);
    // TODO 需要释放 ANativeWindow_release(pWindow);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOG_D("JNI_OnLoad");
    javaVM = vm;// 获取JavaVM
    return JNI_VERSION_1_4;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ff_player_MediaPlayer_native_1prepare(JNIEnv *env, jobject instance, jstring path_) {

    const char *path = env->GetStringUTFChars(path_, nullptr);

    pJavaCallHelper = new JavaCallHelper(javaVM, env, instance);

    pFFmpeg = new MyFFmpeg(pJavaCallHelper, path);
    pFFmpeg->setRenderCallback(renderFrame);
    pFFmpeg->prepare();

    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ff_player_MediaPlayer_native_1start(JNIEnv *env, jobject instance) {

    if (pFFmpeg != nullptr) {
        pFFmpeg->start();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ff_player_MediaPlayer_native_1set_1surface(JNIEnv *env, jobject instance,
                                                    jobject surface) {

    if (pWindow != nullptr) {
        ANativeWindow_release(pWindow);
    }
    // 创建新的窗口用于视频显示
    pWindow = ANativeWindow_fromSurface(env, surface);
}