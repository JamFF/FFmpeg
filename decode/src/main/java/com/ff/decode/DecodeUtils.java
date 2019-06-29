package com.ff.decode;

/**
 * description: 解码多媒体工具类
 * author: FF
 * time: 2019-06-09 17:19
 */
public class DecodeUtils {

    static {
        System.loadLibrary("decode");
    }

    /**
     * 音频解码
     *
     * @param input  输入音频路径
     * @param output 解码后PCM保存路径
     * @return 0成功，-1失败
     */
    public native static int decodeAudio(String input, String output);

    /**
     * 视频解码
     *
     * @param input  输入视频路径
     * @param output 解码后YUV保存路径
     * @return 0成功，-1失败
     */
    public native static int decodeVideo(String input, String output);
}
