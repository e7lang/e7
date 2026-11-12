// Shared configuration for the published e7vm artifacts (group, version, POM, repositories, signing).
//
// Properties (all optional):
//   -Pe7.version=0.1.5 (or env E7_VERSION)   version to publish (default: CMake project version + "-SNAPSHOT")
//   -Pe7.signingKey / -Pe7.signingPassword   ASCII-armored PGP key; or env SIGNING_KEY / SIGNING_PASSWORD
//   env GITHUB_ACTOR / GITHUB_TOKEN          credentials for the GitHubPackages repository
// Every module also publishes to the file repository build/staging-repo ("Staging"), which is what the
// Maven Central bundle (scripts/maven-central-bundle.sh) is made from.

val repoRoot: File = rootDir.resolve("../..").canonicalFile
val cmakeVersion: String = Regex("""project\(e7vm VERSION ([0-9.]+)""")
  .find(repoRoot.resolve("CMakeLists.txt").readText())?.groupValues?.get(1) ?: "0.0.0"
val e7Version: String = (providers.gradleProperty("e7.version").orNull ?: System.getenv("E7_VERSION"))
  ?.trim()?.removePrefix("v")?.takeIf { it.isNotEmpty() }
  ?: "$cmakeVersion-SNAPSHOT"

allprojects {
  group = "org.e7vm"
  version = e7Version
  extra["repoRoot"] = repoRoot
}

val signingKey: String? = (providers.gradleProperty("e7.signingKey").orNull ?: System.getenv("SIGNING_KEY"))
  ?.takeIf { it.isNotBlank() }
val signingPassword: String = providers.gradleProperty("e7.signingPassword").orNull
  ?: System.getenv("SIGNING_PASSWORD") ?: ""

subprojects {
  plugins.withId("maven-publish") {
    apply(plugin = "signing")
    val publishing = extensions.getByType<PublishingExtension>()
    publishing.repositories {
      maven {
        name = "Staging"
        url = uri(rootProject.layout.buildDirectory.dir("staging-repo"))
      }
      maven {
        name = "GitHubPackages"
        url = uri("https://maven.pkg.github.com/e7lang/e7")
        credentials {
          username = System.getenv("GITHUB_ACTOR")
          password = System.getenv("GITHUB_TOKEN")
        }
      }
    }
    publishing.publications.withType<MavenPublication>().configureEach {
      pom {
        url.set("https://github.com/e7lang/e7")
        licenses {
          license {
            name.set("MIT License")
            url.set("https://opensource.org/licenses/MIT")
            distribution.set("repo")
          }
        }
        developers {
          developer {
            id.set("e7lang")
            name.set("The E7 authors")
            url.set("https://github.com/e7lang")
          }
        }
        scm {
          url.set("https://github.com/e7lang/e7")
          connection.set("scm:git:https://github.com/e7lang/e7.git")
          developerConnection.set("scm:git:ssh://git@github.com/e7lang/e7.git")
        }
        issueManagement {
          system.set("GitHub")
          url.set("https://github.com/e7lang/e7/issues")
        }
      }
    }
    extensions.configure<SigningExtension> {
      isRequired = signingKey != null
      if (signingKey != null) {
        useInMemoryPgpKeys(signingKey, signingPassword)
        sign(publishing.publications)
      }
    }
  }
}
