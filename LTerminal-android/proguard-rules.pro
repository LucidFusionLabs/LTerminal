-optimizations !code/simplification/variable

-keep class **.R$* { *; }
-keep public class com.lucidfusionlabs.app.MainActivity { *; }
-keep public class com.lucidfusionlabs.app.MainView { *; }
-keep public class com.lucidfusionlabs.app.Toolbar { *; }
-keep public class com.lucidfusionlabs.app.Screen { *; }
-keep public class com.lucidfusionlabs.app.AlertScreen { *; }
-keep public class com.lucidfusionlabs.app.MenuScreen { *; }
-keep public class com.lucidfusionlabs.app.TableScreen { *; }
-keep public class com.lucidfusionlabs.app.TextScreen { *; }
-keep public class com.lucidfusionlabs.app.ScreenFragmentNavigator { *; }
-keep public class com.lucidfusionlabs.app.ModelItemRecyclerViewAdapter { *; }
-keep public class com.lucidfusionlabs.core.NativeCallback { *; }
-keep public class com.lucidfusionlabs.core.NativeStringCB { *; }
-keep public class com.lucidfusionlabs.core.NativeIntIntCB { *; }
-keep public class com.lucidfusionlabs.core.NativePickerItemCB { *; }
-keep public class com.lucidfusionlabs.core.PickerItem { *; }
-keep public class com.lucidfusionlabs.core.ModelItem { *; }
-keep public class com.lucidfusionlabs.core.ModelItemChange { *; }
-keep public class com.lucidfusionlabs.core.ModelItemLinearLayout { *; }
-keep public class com.lucidfusionlabs.ads.Advertising { *; }
-keep public class com.lucidfusionlabs.billing.PurchaseManager { *; }

-keep public class * extends android.app.Activity
-keep public class * extends android.app.Fragment
-keep public class * extends android.support.v4.app.Fragment

-keep public class com.google.android.gms.* { public *; }
-dontwarn com.google.android.gms.**
