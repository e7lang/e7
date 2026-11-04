# e7vm: the JNI library looks up these classes, constructors, fields and interface methods by name
# (E7Exception(int,String), KeyHandle.id, NativeFunction.invoke, KeyProvider.key, PrintHandler.print).
-keep class org.e7vm.** { *; }
-keep interface org.e7vm.** { *; }
-keepclasseswithmembernames,includedescriptorclasses class org.e7vm.** {
    native <methods>;
}
# Host implementations of the callback interfaces are invoked from native code.
-keepclassmembers class * implements org.e7vm.E7VM$NativeFunction {
    public java.lang.Object invoke(java.lang.Object[]);
}
-keepclassmembers class * implements org.e7vm.E7VM$KeyProvider {
    public byte[] key(long);
}
-keepclassmembers class * implements org.e7vm.E7VM$PrintHandler {
    public void print(java.lang.String);
}
