package com.ff.video;

import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * description: 视频播放器，不播放音频
 * author: FF
 * time: 2019-06-08 17:16
 */
public class VideoPlayer implements SurfaceHolder.Callback {

    static {
        System.loadLibrary("video_player");
    }

    private SurfaceHolder mHolder;

    public void setSurfaceView(SurfaceView surfaceView) {
        if (mHolder != null) {
            this.mHolder.removeCallback(this);
        }
        this.mHolder = surfaceView.getHolder();
        this.mHolder.addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {

    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        mHolder = holder;
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {

    }

    public void playVideo(String path) {
        playVideo(path, mHolder.getSurface());
    }

    /**
     * 播放视频
     *
     * @param path
     * @param surface
     */
    private native void playVideo(String path, Surface surface);

    /**
     * 停止播放
     */
    public native void stopVideo();
}
