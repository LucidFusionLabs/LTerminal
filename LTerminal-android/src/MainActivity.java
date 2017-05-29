package com.lucidfusionlabs.LTerminal;

import android.util.Log;
import com.crashlytics.android.Crashlytics;
import com.crashlytics.android.ndk.CrashlyticsNdk;
import io.fabric.sdk.android.Fabric;

public class MainActivity extends com.lucidfusionlabs.app.MainActivity {
    @Override protected void onCreated() {
        if (preferences.getString("send_crash_reports", "1").equals("1")) {
            if (BuildConfig.DEBUG) Log.i("lfl", "BuildConfig.DEBUG skipping Crashlytics");
            else {
                Log.i("lfl", "Initializing Crashlytics");
                Fabric.with(this, new Crashlytics(), new CrashlyticsNdk());
                String crash_report_name  = preferences.getString("crash_report_name",  "");
                String crash_report_email = preferences.getString("crash_report_email", "");
                if (crash_report_name .length() > 0) Crashlytics.setUserName (crash_report_name);
                if (crash_report_email.length() > 0) Crashlytics.setUserEmail(crash_report_email);
            }
        }
    }
}
