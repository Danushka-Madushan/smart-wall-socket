# SocketMan ProGuard / R8 Rules
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses
-keepattributes EnclosingMethod

# Keep SocketMan data models used in state & persistence
-keep class nibm.iot.socketman.data.** { *; }

# Preserve native JNI methods (CameraX and Barhopper)
-keepclasseswithmembernames class * {
    native <methods>;
}

# CameraX: Keep all CameraX APIs, implementations, and internal reflection factories
-keep class androidx.camera.** { *; }
-keep interface androidx.camera.** { *; }
-dontwarn androidx.camera.**

# ML Kit Barcode Scanning & Vision Models
-keep class com.google.mlkit.** { *; }
-keep interface com.google.mlkit.** { *; }
-dontwarn com.google.mlkit.**
-keep class com.google.android.gms.internal.mlkit_vision_barcode.** { *; }
-dontwarn com.google.android.gms.**
