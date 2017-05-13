-optimizations !code/simplification/variable

-keep class **.R$* { *; }
-keep class com.lucidfusionlabs.app.MainView { *; }
-keep public class com.lucidfusionlabs.app.MainActivity { *; }
-keep public class com.lucidfusionlabs.app.MainView { *; }
-keep public class com.lucidfusionlabs.app.NativeCallback { *; }
-keep public class com.lucidfusionlabs.app.NativeStringCB { *; }
-keep public class com.lucidfusionlabs.app.NativeIntIntCB { *; }
-keep public class com.lucidfusionlabs.app.NativePickerItemCB { *; }
-keep public class com.lucidfusionlabs.app.PickerItem { *; }
-keep public class com.lucidfusionlabs.app.ModelItem { *; }
-keep public class com.lucidfusionlabs.app.ModelItemChange { *; }
-keep public class com.lucidfusionlabs.app.Toolbar { *; }
-keep public class com.lucidfusionlabs.app.Screen { *; }
-keep public class com.lucidfusionlabs.app.AlertScreen { *; }
-keep public class com.lucidfusionlabs.app.MenuScreen { *; }
-keep public class com.lucidfusionlabs.app.TableScreen { *; }
-keep public class com.lucidfusionlabs.app.TextViewScreen { *; }
-keep public class com.lucidfusionlabs.app.ScreenFragmentNavigator { *; }
-keep public class com.lucidfusionlabs.app.ModelItemRecyclerViewAdapter { *; }

-keep public class * extends android.app.Activity
-keep public class * extends android.app.Fragment
-keep public class * extends android.support.v4.app.Fragment

-keep public class com.google.android.gms.* { public *; }
-dontwarn com.google.android.gms.**
