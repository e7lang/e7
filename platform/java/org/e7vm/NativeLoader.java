package org.e7vm;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * Loads libe7vm_jni. Order:
 * <ol>
 *   <li>system property {@code e7vm.library} (absolute path to the library file), if set;</li>
 *   <li>{@code System.loadLibrary("e7vm_jni")} (Android APKs, {@code java.library.path});</li>
 *   <li>the copy bundled in the org.e7vm:e7vm jar under {@code org/e7vm/native/<platform>/},
 *       extracted to a temporary file (desktop JVMs).</li>
 * </ol>
 * Uses java.io only so that it also verifies on old Android runtimes.
 */
final class NativeLoader {
  private static final String NAME = "e7vm_jni";
  private static boolean loaded;

  private NativeLoader() {}

  static synchronized void load() {
    if (loaded) return;
    String explicit = System.getProperty("e7vm.library");
    if (explicit != null && !explicit.isEmpty()) {
      System.load(new File(explicit).getAbsolutePath());
      loaded = true;
      return;
    }
    UnsatisfiedLinkError first;
    try {
      System.loadLibrary(NAME);
      loaded = true;
      return;
    } catch (UnsatisfiedLinkError e) {
      first = e;
    }
    String file = System.mapLibraryName(NAME);
    for (String platform : platforms()) {
      InputStream in = NativeLoader.class.getResourceAsStream("/org/e7vm/native/" + platform + "/" + file);
      if (in == null) continue;
      try {
        File dir = File.createTempFile("e7vm", "");
        if (!dir.delete() || !dir.mkdir()) throw new IOException("cannot create " + dir);
        File lib = new File(dir, file);
        OutputStream out = new FileOutputStream(lib);
        try {
          byte[] buf = new byte[1 << 16];
          for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
        } finally {
          out.close();
        }
        lib.deleteOnExit();
        dir.deleteOnExit();
        System.load(lib.getAbsolutePath());
        loaded = true;
        return;
      } catch (IOException e) {
        UnsatisfiedLinkError err = new UnsatisfiedLinkError("cannot extract bundled " + file + ": " + e);
        err.initCause(e);
        throw err;
      } finally {
        try { in.close(); } catch (IOException ignored) { }
      }
    }
    UnsatisfiedLinkError err = new UnsatisfiedLinkError(first.getMessage()
        + " (no bundled e7vm JNI library for " + System.getProperty("os.name") + "/" + System.getProperty("os.arch")
        + "; build libe7vm_jni with -DE7_BUILD_JNI=ON and put it on java.library.path or set -De7vm.library=<path>)");
    err.initCause(first);
    throw err;
  }

  private static String[] platforms() {
    String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
    String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
    if (arch.equals("amd64") || arch.equals("x86-64")) arch = "x86_64";
    if (arch.equals("arm64")) arch = "aarch64";
    if (os.contains("mac") || os.contains("darwin")) return new String[] {"macos-" + arch, "macos"};
    if (os.contains("win")) return new String[] {"windows-" + arch};
    if (os.contains("linux")) return new String[] {"linux-" + arch};
    return new String[0];
  }
}
