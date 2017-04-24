-optimizations !code/simplification/variable

-keep class **.R$* { *; }
-keep class com.lucidfusionlabs.app.MainView { *; }
-keep public class com.lucidfusionlabs.app.MainActivity { *; }
-keep public class com.lucidfusionlabs.app.LCallback { *; }
-keep public class com.lucidfusionlabs.app.LStringCB { *; }
-keep public class com.lucidfusionlabs.app.LIntIntCB { *; }
-keep public class com.lucidfusionlabs.app.LPickerItemCB { *; }
-keep public class com.lucidfusionlabs.app.JPickerItem { *; }
-keep public class com.lucidfusionlabs.app.JModelItem { *; }
-keep public class com.lucidfusionlabs.app.JModelItemChange { *; }
-keep public class com.lucidfusionlabs.app.JWidget { *; }
-keep public class com.lucidfusionlabs.app.JAlert { *; }
-keep public class com.lucidfusionlabs.app.JToolbar { *; }
-keep public class com.lucidfusionlabs.app.JMenu { *; }
-keep public class com.lucidfusionlabs.app.JTable { *; }
-keep public class com.lucidfusionlabs.app.JTextView { *; }
-keep public class com.lucidfusionlabs.app.JNavigation { *; }

-keep public class * extends android.app.Activity
-keep public class * extends android.app.Fragment
-keep public class * extends android.support.v4.app.Fragment

-keep public class com.google.android.gms.* { public *; }
-dontwarn com.google.android.gms.**
