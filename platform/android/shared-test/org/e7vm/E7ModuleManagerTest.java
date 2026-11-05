package org.e7vm;

import org.junit.Test;

/** JUnit entry points for {@link ModuleManagerScenarios} (desktop JVM and Android instrumented tests). */
public class E7ModuleManagerTest {
  @Test public void happyPath() throws Exception { ModuleManagerScenarios.happyPath(); }
  @Test public void badSha256() throws Exception { ModuleManagerScenarios.badSha256(); }
  @Test public void badManifestSignature() throws Exception { ModuleManagerScenarios.badManifestSignature(); }
  @Test public void killSwitch() throws Exception { ModuleManagerScenarios.killSwitch(); }
  @Test public void rolloutExclusion() throws Exception { ModuleManagerScenarios.rolloutExclusion(); }
  @Test public void requiresNewerVm() throws Exception { ModuleManagerScenarios.requiresNewerVm(); }
  @Test public void selfTestFailure() throws Exception { ModuleManagerScenarios.selfTestFailure(); }
  @Test public void rollbackAfterFailures() throws Exception { ModuleManagerScenarios.rollbackAfterFailures(); }
}
