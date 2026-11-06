package org.e7vm;

import org.junit.Test;

/** JUnit entry points for {@link AsyncScenarios} (desktop JVM and Android instrumented tests). */
public class E7VMAsyncTest {
  @Test public void settledLaterFromAnotherThread() throws Exception { AsyncScenarios.settledLaterFromAnotherThread(); }
  @Test public void settledAtOnce() throws Exception { AsyncScenarios.settledAtOnce(); }
  @Test public void rejectedAndCaught() throws Exception { AsyncScenarios.rejectedAndCaught(); }
  @Test public void promiseAllReverseOrder() throws Exception { AsyncScenarios.promiseAllReverseOrder(); }
  @Test public void pendingCallBlocksSecondCall() throws Exception { AsyncScenarios.pendingCallBlocksSecondCall(); }
  @Test public void abandonPendingCall() throws Exception { AsyncScenarios.abandonPendingCall(); }
  @Test public void syncCallOfPendingExport() throws Exception { AsyncScenarios.syncCallOfPendingExport(); }
  @Test public void topLevelAwait() throws Exception { AsyncScenarios.topLevelAwait(); }
}
