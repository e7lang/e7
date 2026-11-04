// org.e7vm:e7vm — the Java API as a plain JVM jar.
//
// Desktop JNI libraries are bundled when -Pe7.nativesDir=<dir> is given; <dir> holds one folder per
// platform (linux-x86_64, linux-aarch64, macos, windows-x86_64) with libe7vm_jni.so / .dylib / e7vm_jni.dll.
// They are packaged as org/e7vm/native/<platform>/<lib> and extracted by org.e7vm.NativeLoader at runtime.
// Tests: -Pe7.nativesDir (bundled path) or -Pe7.libraryPath=<dir containing libe7vm_jni> (java.library.path).
plugins {
  `java-library`
  `maven-publish`
}

val repoRoot = rootProject.extra["repoRoot"] as File
val nativesDir = providers.gradleProperty("e7.nativesDir").orNull?.let { file(it) }
val libraryPath = providers.gradleProperty("e7.libraryPath").orNull

base { archivesName.set("e7vm") }

java {
  withSourcesJar()
  withJavadocJar()
}

tasks.withType<JavaCompile>().configureEach {
  options.encoding = "UTF-8"
  options.compilerArgs.add("-Xlint:-options")
}
tasks.named<JavaCompile>("compileJava") { options.release.set(8) }
tasks.named<JavaCompile>("compileTestJava") { options.release.set(11) }

sourceSets {
  main {
    java {
      setSrcDirs(listOf(repoRoot.resolve("platform/java")))
      include("org/e7vm/**")
    }
    resources.setSrcDirs(emptyList<File>())
  }
  test {
    java.setSrcDirs(listOf(file("../shared-test")))
    resources {
      setSrcDirs(listOf(repoRoot.resolve("examples/embed")))
      include("sign.ts")
    }
  }
}

tasks.named<Jar>("jar") {
  manifest {
    attributes(
      "Automatic-Module-Name" to "org.e7vm",
      "Implementation-Title" to "e7vm",
      "Implementation-Version" to project.version.toString(),
    )
  }
}

tasks.named<ProcessResources>("processResources") {
  if (nativesDir != null) {
    from(nativesDir) { into("org/e7vm/native") }
  }
}

tasks.named<Javadoc>("javadoc") {
  (options as StandardJavadocDocletOptions).apply {
    encoding = "UTF-8"
    addBooleanOption("Xdoclint:none", true)
    addStringOption("source", "8")
  }
  isFailOnError = false
}

dependencies {
  testImplementation("junit:junit:4.13.2")
}

tasks.named<Test>("test") {
  useJUnit()
  if (libraryPath != null) systemProperty("java.library.path", file(libraryPath).absolutePath)
  testLogging {
    events("passed", "failed", "skipped")
    showStandardStreams = true
    exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
  }
}

publishing {
  publications {
    create<MavenPublication>("maven") {
      artifactId = "e7vm"
      from(components["java"])
      pom {
        name.set("e7vm")
        description.set(
          "Java binding of e7vm, an embeddable bytecode VM for the E7 language (a strict TypeScript subset). " +
            "Bundles JNI libraries for Linux x86_64/aarch64, macOS and Windows x86_64."
        )
      }
    }
  }
}
