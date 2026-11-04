// Gradle build for the e7vm Maven artifacts:
//   :library -> org.e7vm:e7vm-android (AAR: Java API + libe7vm_jni.so for 4 ABIs, built from the repo's CMakeLists.txt)
//   :jvm     -> org.e7vm:e7vm         (JAR: Java API + optional bundled desktop JNI libraries)
// Pass -Pe7.jvmOnly=true to configure only :jvm (no Android SDK / Google Maven needed).
pluginManagement {
  repositories {
    google()
    mavenCentral()
    gradlePluginPortal()
  }
}

dependencyResolutionManagement {
  repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
  repositories {
    google()
    mavenCentral()
  }
}

rootProject.name = "e7vm-gradle"

include(":jvm")
if (providers.gradleProperty("e7.jvmOnly").orNull != "true") {
  include(":library")
}
