// org.e7vm:e7vm-android — AAR with the Java API (platform/java/org/e7vm) and libe7vm_jni.so for
// arm64-v8a, armeabi-v7a, x86_64 and x86, built from the repository's CMakeLists.txt.
//   -Pe7.abis=x86_64            restrict ABIs (e.g. for emulator tests)
//   env E7_NDK_PATH=<ndk dir>   use this NDK instead of ndkVersion below
plugins {
  id("com.android.library") version "9.5.0"
  `maven-publish`
}

val repoRoot = rootProject.extra["repoRoot"] as File
val abis = (providers.gradleProperty("e7.abis").orNull ?: "arm64-v8a,armeabi-v7a,x86_64,x86")
  .split(',').map { it.trim() }.filter { it.isNotEmpty() }

// The Java sources live next to the desktop test in platform/java; copy just org/e7vm into a
// generated source folder so that only the library classes end up in the AAR.
// (A plain path rather than a task provider: AGP 9 rejects providers in the source set DSL; the task
// dependencies are wired explicitly below.)
val generatedJava: File = layout.buildDirectory.dir("generated/e7java").get().asFile
val syncJavaSources = tasks.register<Sync>("syncE7JavaSources") {
  from(repoRoot.resolve("platform/java")) { include("org/e7vm/**") }
  into(generatedJava)
}

android {
  namespace = "org.e7vm"
  compileSdk = 36
  System.getenv("E7_NDK_PATH")?.takeIf { it.isNotBlank() }?.let { ndkPath = it } ?: run { ndkVersion = "28.2.13676358" }

  defaultConfig {
    minSdk = 21
    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    consumerProguardFiles("consumer-rules.pro")
    ndk { abiFilters += abis }
    externalNativeBuild {
      cmake {
        arguments += listOf(
          "-DCMAKE_BUILD_TYPE=Release",
          "-DE7_BUILD_JNI=ON",
          "-DE7_BUILD_TESTS=OFF",
          "-DE7_BUILD_TOOLS=OFF",
          "-DE7_WITH_COMPILER=ON",
          "-DANDROID_STL=c++_static",
          "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
        )
        targets += "e7vm_jni"
      }
    }
  }

  externalNativeBuild {
    cmake {
      path = repoRoot.resolve("CMakeLists.txt")
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
  }

  sourceSets {
    getByName("main") {
      java.directories.add(generatedJava.absolutePath)
    }
    getByName("androidTest") {
      java.directories.add(file("../shared-test").absolutePath)
      resources.directories.add(repoRoot.resolve("examples/embed").absolutePath)
    }
  }

  buildTypes {
    release {
      isMinifyEnabled = false
    }
  }

  publishing {
    singleVariant("release") {
      withSourcesJar()
      withJavadocJar()
    }
  }
}

tasks.named("preBuild") { dependsOn(syncJavaSources) }
tasks.configureEach {
  if (name != syncJavaSources.name && (name.contains("SourcesJar") || name.contains("avadoc") || name.contains("javaDoc") ||
      name.contains("Lint", ignoreCase = true))) {
    dependsOn(syncJavaSources)
  }
}

dependencies {
  androidTestImplementation("androidx.test:runner:1.7.0")
  androidTestImplementation("androidx.test.ext:junit:1.3.0")
  androidTestImplementation("junit:junit:4.13.2")
}

publishing {
  publications {
    register<MavenPublication>("release") {
      artifactId = "e7vm-android"
      afterEvaluate { from(components["release"]) }
      pom {
        name.set("e7vm-android")
        description.set(
          "Android binding of e7vm, an embeddable bytecode VM for the E7 language (a strict TypeScript subset): " +
            "Java/Kotlin API plus libe7vm_jni for arm64-v8a, armeabi-v7a, x86_64 and x86."
        )
      }
    }
  }
}
