# SocketMan ProGuard / R8 Rules
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses
-keepattributes EnclosingMethod

# Keep data models used in state
-keep class nibm.iot.socketman.data.** { *; }
