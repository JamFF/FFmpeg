package com.ff.video;

import android.Manifest;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.List;

import pub.devrel.easypermissions.AfterPermissionGranted;
import pub.devrel.easypermissions.AppSettingsDialog;
import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements View.OnClickListener,
        EasyPermissions.PermissionCallbacks {

    private static final String TAG = "VideoPlayer";
    private static final String TAG_PERMISSION = "Permission";
    private static final int PERMISSION_STORAGE_CODE = 10001;
    private static final String PERMISSION_STORAGE_MSG = "需要SD卡读写权限，否则无法正常使用";
    private static final String[] PERMS = {Manifest.permission.WRITE_EXTERNAL_STORAGE};
    private SurfaceView mSurfaceView;
    private VideoPlayer mPlayer;
    private File mFile;
    private boolean taskRunning;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        mSurfaceView = findViewById(R.id.surfaceView);
        findViewById(R.id.play_video).setOnClickListener(this);
        findViewById(R.id.stop_video).setOnClickListener(this);

        initData();
    }

    private void initData() {
        mPlayer = new VideoPlayer();
        mPlayer.setSurfaceView(mSurfaceView);
        mFile = new File(Environment.getExternalStorageDirectory(), "input.mp4");
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.play_video:
                playVideo();
                break;
            case R.id.stop_video:
                mPlayer.stopVideo();
                break;
        }
    }

    @AfterPermissionGranted(PERMISSION_STORAGE_CODE)
    private void playVideo() {
        if (!EasyPermissions.hasPermissions(this, PERMS)) {
            // 申请权限
            EasyPermissions.requestPermissions(this, PERMISSION_STORAGE_MSG,
                    PERMISSION_STORAGE_CODE, PERMS);
            return;
        }
        if (taskRunning) {
            Toast.makeText(this, "当前正在播放", Toast.LENGTH_SHORT).show();
        } else {
            new VideoPlayTask(this).execute(mFile);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onPermissionsGranted(int requestCode, @NonNull List<String> perms) {
        if (requestCode == PERMISSION_STORAGE_CODE) {
            Log.d(TAG_PERMISSION, "onPermissionsGranted: ");
        }
    }

    @Override
    public void onPermissionsDenied(int requestCode, @NonNull List<String> perms) {
        if (EasyPermissions.somePermissionPermanentlyDenied(this, perms)) {
            // 拒绝权限，并不再询问
            new AppSettingsDialog
                    .Builder(this)
                    .setTitle("授权提醒")
                    .setRationale(PERMISSION_STORAGE_MSG)
                    .setPositiveButton("打开设置")
                    .setNegativeButton("取消")
                    .build()
                    .show();
        } else {
            // 拒绝权限
            if (requestCode == PERMISSION_STORAGE_CODE) {
                Log.d(TAG_PERMISSION, "onPermissionsDenied: ");
            }
        }
    }

    @Override
    protected void onDestroy() {
        mPlayer.stopVideo();
        super.onDestroy();
    }

    private static class VideoPlayTask extends AsyncTask<File, Void, String> {

        private WeakReference<MainActivity> mReference;

        private VideoPlayTask(MainActivity activity) {
            mReference = new WeakReference<>(activity);
        }

        @Override
        protected void onPreExecute() {
            if (mReference != null && mReference.get() != null) {
                mReference.get().taskRunning = true;
            }
            Log.d(TAG, "开始播放");
        }

        @Override
        protected String doInBackground(File... files) {
            if (mReference != null && mReference.get() != null) {
                MainActivity activity = mReference.get();
                activity.mPlayer.playVideo(files[0].getAbsolutePath());
            }
            return "停止播放";
        }

        @Override
        protected void onPostExecute(String result) {
            if (mReference != null && mReference.get() != null) {
                mReference.get().taskRunning = false;
            }
            Log.d(TAG, result);
        }

        @Override
        protected void onCancelled() {
            Log.d(TAG, "取消播放");
        }
    }
}
