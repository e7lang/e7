import org.e7vm.ModuleManagerScenarios;

/**
 * Runs the E7ModuleManager scenarios (platform/android/shared-test/org/e7vm/ModuleManagerScenarios.java)
 * without JUnit, like E7VMTest:
 *   javac -d classes platform/java/org/e7vm/*.java platform/java/test/*.java \
 *       platform/android/shared-test/org/e7vm/ModuleManagerScenarios.java platform/android/shared-test/org/e7vm/AsyncScenarios.java
 *   java -Djava.library.path=build -cp classes ModuleManagerTest
 */
public class ModuleManagerTest {
  public static void main(String[] a) {
    int failures = 0;
    for (String name : ModuleManagerScenarios.ALL) {
      try {
        ModuleManagerScenarios.run(name);
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
    System.out.println("ModuleManagerTest: all scenarios passed");
  }
}
