package org.e7vm;

/** Error raised by the E7 virtual machine. {@link #status} mirrors e7_status in e7.h. */
public final class E7Exception extends RuntimeException {
  public static final int SUSPENDED = 1, RUNTIME = 2, BUDGET = 3, MEMORY = 4, STACK = 5, LOAD = 6, VERIFY = 7,
      SIGNATURE = 8, NOT_FOUND = 9, ARGS = 10, COMPILE = 11, STATE = 12;
  public final int status;

  public E7Exception(int status, String message) {
    super(message);
    this.status = status;
  }
}
