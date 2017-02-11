package com.lucidfusionlabs.LTerminal;

import android.util.Log;
import com.crashlytics.android.Crashlytics;
import com.crashlytics.android.ndk.CrashlyticsNdk;
import io.fabric.sdk.android.Fabric;

public class MainActivity extends com.lucidfusionlabs.app.MainActivity {
    public MainActivity() {
        preference_fragment = new com.lucidfusionlabs.app.PreferenceFragment(R.layout.preferences);
    }

    @Override protected void onCreated() {
        if (!BuildConfig.DEBUG) {
            Log.i("lfl", "Initializing Crashlytics");
            Fabric.with(this, new Crashlytics(), new CrashlyticsNdk());
        }
    }
}
