package org.mozilla.gecko;

import android.Manifest;
import android.annotation.TargetApi;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.ArrayMap;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import com.qq.e.ads.splash.SplashAD;
import com.qq.e.ads.splash.SplashADListener;
import com.qq.e.comm.util.AdError;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;


public class SplashActivity extends Activity implements SplashADListener {

  private SplashAD splashAD;
  private ViewGroup container;
  private TextView skipView;
  private ImageView splashHolder;
  private static final String SKIP_TEXT = "点击跳过 %d";
  
  public boolean canJump = false;
  private boolean needStartLauncher = true;


  private int minSplashTimeWhenNoAD = 2000;

  private long fetchSplashADTime = 0;
  private Handler handler = new Handler(Looper.getMainLooper());

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_splash);
    container = (ViewGroup) this.findViewById(R.id.splash_container);
    skipView = (TextView) findViewById(R.id.skip_view);
    splashHolder = (ImageView) findViewById(R.id.splash_holder);
    boolean needLogo = getIntent().getBooleanExtra("need_logo", false);
    needStartLauncher = getIntent().getBooleanExtra("need_start_demo_list", true);
    if (!needLogo) {
      findViewById(R.id.app_logo).setVisibility(View.GONE);
    }

    if (Build.VERSION.SDK_INT >= 23) {
      checkAndRequestPermission();
    } else {

      fetchSplashAD(this, container, skipView, Constants.APPID, getPosId(), this, 0);
    }
  }

  private String getPosId() {
    String posId = getIntent().getStringExtra("pos_id");
    return TextUtils.isEmpty(posId) ? Constants.SplashPosID : posId;
  }


  @TargetApi(Build.VERSION_CODES.M)
  private void checkAndRequestPermission() {
    List<String> lackedPermission = new ArrayList<String>();
    if (!(checkSelfPermission(Manifest.permission.READ_PHONE_STATE) == PackageManager.PERMISSION_GRANTED)) {
      lackedPermission.add(Manifest.permission.READ_PHONE_STATE);
    }

    if (!(checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED)) {
      lackedPermission.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
    }

    if (!(checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED)) {
      lackedPermission.add(Manifest.permission.ACCESS_FINE_LOCATION);
    }


    if (lackedPermission.size() == 0) {
      fetchSplashAD(this, container, skipView, Constants.APPID, getPosId(), this, 0);
    } else {

      String[] requestPermissions = new String[lackedPermission.size()];
      lackedPermission.toArray(requestPermissions);
      requestPermissions(requestPermissions, 1024);
    }
  }

  private boolean hasAllPermissionsGranted(int[] grantResults) {
    for (int grantResult : grantResults) {
      if (grantResult == PackageManager.PERMISSION_DENIED) {
        return false;
      }
    }
    return true;
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    if (requestCode == 1024 && hasAllPermissionsGranted(grantResults)) {
      fetchSplashAD(this, container, skipView, Constants.APPID, getPosId(), this, 0);
    } else {

      Toast.makeText(this, "应用缺少必要的权限！请点击\"权限\"，打开所需要的权限。", Toast.LENGTH_LONG).show();
      Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
      intent.setData(Uri.parse("package:" + getPackageName()));
      startActivity(intent);
      finish();
    }
  }


  private void fetchSplashAD(Activity activity, ViewGroup adContainer, View skipContainer,
      String appId, String posId, SplashADListener adListener, int fetchDelay) {
    fetchSplashADTime = System.currentTimeMillis();
    Map<String, String> tags = new HashMap<>();
    tags.put("tag_s1", "value_s1");
    tags.put("tag_s2", "value_s2");

    splashAD = new SplashAD(activity, adContainer, skipContainer, appId, posId, adListener,
        fetchDelay, tags);

  }

  @Override
  public void onADPresent() {
    Log.i("GDT", "SplashADPresent");
    splashHolder.setVisibility(View.INVISIBLE);
  }

  @Override
  public void onADClicked() {
    Log.i("GDT", "SplashADClicked clickUrl: "
        + (splashAD.getExt() != null ? splashAD.getExt().get("clickUrl") : ""));
  }

  @Override
  public void onADTick(long millisUntilFinished) {
    Log.i("GDT", "SplashADTick " + millisUntilFinished + "ms");
    skipView.setText(String.format(SKIP_TEXT, Math.round(millisUntilFinished / 1000f)));
  }

  @Override
  public void onADExposure() {
    Log.i("GDT", "SplashADExposure");
  }

  @Override
  public void onADDismissed() {
    Log.i("GDT", "SplashADDismissed");
    next();
  }

  @Override
  public void onNoAD(AdError error) {
    Log.i(
        "GDT",
        String.format("LoadSplashADFail, eCode=%d, errorMsg=%s", error.getErrorCode(),
            error.getErrorMsg()));

    long alreadyDelayMills = System.currentTimeMillis() - fetchSplashADTime;//从拉广告开始到onNoAD已经消耗了多少时间
    long shouldDelayMills = alreadyDelayMills > minSplashTimeWhenNoAD ? 0 : minSplashTimeWhenNoAD
        - alreadyDelayMills;
    handler.postDelayed(new Runnable() {
      @Override
      public void run() {
        if (needStartLauncher) {
          SplashActivity.this.startActivity(new Intent(SplashActivity.this, LauncherActivity.class));
        }
        SplashActivity.this.finish();
      }
    }, shouldDelayMills);
  }


  private void next() {
    if (canJump) {
      if (needStartLauncher) {
        this.startActivity(new Intent(this, LauncherActivity.class));
      }
      this.finish();
    } else {
      canJump = true;
    }
  }

  @Override
  protected void onPause() {
    super.onPause();
    canJump = false;
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (canJump) {
      next();
    }
    canJump = true;
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    handler.removeCallbacksAndMessages(null);
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) {
    if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_HOME) {
      return true;
    }
    return super.onKeyDown(keyCode, event);
  }

}
