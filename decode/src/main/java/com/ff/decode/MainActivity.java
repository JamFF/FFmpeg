package com.ff.decode;

import android.Manifest;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.List;

import pub.devrel.easypermissions.AfterPermissionGranted;
import pub.devrel.easypermissions.AppSettingsDialog;
import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements View.OnClickListener,
        EasyPermissions.PermissionCallbacks {

    private static final String TAG = "Decode";
    private static final String TAG_PERMISSION = "Permission";
    private static final int PERMISSION_STORAGE_CODE_AUDIO = 10001;
    private static final int PERMISSION_STORAGE_CODE_VIDEO = 10002;
    private static final String PERMISSION_STORAGE_MSG = "需要SD卡读取权限，否则无法正常使用";
    private static final String[] PERMS = {Manifest.permission.WRITE_EXTERNAL_STORAGE};

    private static final String AUDIO_INPUT = "Love_Story.mp3";
    private static final String AUDIO_OUTPUT = "Love_Story.pcm";

    private File audioInput, audioOutput;
    private boolean taskRunning;

    private LinearLayout ll_progress;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        ll_progress = findViewById(R.id.ll_progress);
        findViewById(R.id.decode_audio).setOnClickListener(this);
        findViewById(R.id.decode_video).setOnClickListener(this);

        initData();
    }

    private void initData() {
        audioInput = new File(Environment.getExternalStorageDirectory(), AUDIO_INPUT);
        audioOutput = new File(Environment.getExternalStorageDirectory(), AUDIO_OUTPUT);
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.decode_audio:
                audioDecode();
                break;
            case R.id.decode_video:
                videoDecode();
                break;
        }
    }

    @AfterPermissionGranted(PERMISSION_STORAGE_CODE_AUDIO)
    private void audioDecode() {
        if (!EasyPermissions.hasPermissions(this, PERMS)) {
            // 申请权限
            EasyPermissions.requestPermissions(this, PERMISSION_STORAGE_MSG,
                    PERMISSION_STORAGE_CODE_AUDIO, PERMS);
            return;
        }
        if (taskRunning) {
            Toast.makeText(this, "当前正在解码", Toast.LENGTH_SHORT).show();
        } else {
            new DecodeTask(this).execute(audioInput, audioOutput);
        }
    }

    @AfterPermissionGranted(PERMISSION_STORAGE_CODE_VIDEO)
    private void videoDecode() {
        if (!EasyPermissions.hasPermissions(this, PERMS)) {
            // 申请权限
            EasyPermissions.requestPermissions(this, PERMISSION_STORAGE_MSG,
                    PERMISSION_STORAGE_CODE_VIDEO, PERMS);
            return;
        }
        if (taskRunning) {
            Toast.makeText(this, "当前正在解码", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "该功能暂未实现", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onPermissionsGranted(int requestCode, @NonNull List<String> perms) {
        if (requestCode == PERMISSION_STORAGE_CODE_AUDIO) {
            Log.d(TAG_PERMISSION, "onPermissionsGranted: AUDIO");
        } else if (requestCode == PERMISSION_STORAGE_CODE_VIDEO) {
            Log.d(TAG_PERMISSION, "onPermissionsGranted: VIDEO");
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
            if (requestCode == PERMISSION_STORAGE_CODE_AUDIO) {
                Log.d(TAG_PERMISSION, "onPermissionsDenied: AUDIO");
            } else if (requestCode == PERMISSION_STORAGE_CODE_VIDEO) {
                Log.d(TAG_PERMISSION, "onPermissionsDenied: VIDEO");
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (taskRunning) {
            Toast.makeText(this, "当前正在解码", Toast.LENGTH_SHORT).show();
            return;
        }
        super.onBackPressed();
    }

    private void showProgress() {
        ll_progress.setVisibility(View.VISIBLE);
    }

    private void dismissProgress() {
        ll_progress.setVisibility(View.GONE);
    }

    private static class DecodeTask extends AsyncTask<File, Void, String> {

        private WeakReference<MainActivity> mReference;

        private DecodeTask(MainActivity activity) {
            mReference = new WeakReference<>(activity);
        }

        @Override
        protected void onPreExecute() {
            Log.d(TAG, "开始解码");
            if (mReference != null && mReference.get() != null) {
                mReference.get().taskRunning = true;
                mReference.get().showProgress();
            }
        }

        @Override
        protected String doInBackground(File... files) {
            DecodeUtils.decodeAudio(files[0].getAbsolutePath(), files[1].getAbsolutePath());
            return "完成解码";
        }

        @Override
        protected void onPostExecute(String result) {
            if (mReference != null && mReference.get() != null) {
                Toast.makeText(mReference.get(), result, Toast.LENGTH_SHORT).show();
                mReference.get().dismissProgress();
                mReference.get().taskRunning = false;
            }
            Log.d(TAG, result);
        }

        @Override
        protected void onCancelled() {
            Log.d(TAG, "取消解码");
            if (mReference != null && mReference.get() != null) {
                mReference.get().dismissProgress();
                mReference.get().taskRunning = false;
            }
        }
    }
}
