import org.e7vm.AsyncScenarios;

/**
 * Runs the async host function scenarios (platform/android/shared-test/org/e7vm/AsyncScenarios.java)
 * without JUnit, like E7VMTest:
 *   javac -d classes platform/java/org/e7vm/*.java platform/java/test/*.java \
 *       platform/android/shared-test/org/e7vm/ModuleManagerScenarios.java \
 *       platform/android/shared-test/org/e7vm/AsyncScenarios.java
 *   java -Djava.library.path=build -cp classes AsyncTest
 */
public class AsyncTest {
  public static void main(String[] a) {
    int failures = 0;
    for (String name : AsyncScenarios.ALL) {
      try {
        AsyncScenarios.run(name);
        System.out.println("ok   " + name);
      } catch (Throwable t) {
        failures++;
        System.out.println("FAIL " + name + ": " + t);
        t.printStackTrace();
      }
    }
    if (failures > 0) {
      System.err.println(failures + " scenario(s) failed");
      System.exit(1);
    }
    System.out.println("AsyncTest: all scenarios passed");
  }
}
