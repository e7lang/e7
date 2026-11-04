package org.e7vm;

/** Reference to a secret held by the host; resolved through {@link E7VM.KeyProvider}. */
public final class KeyHandle {
  public final long id;

  public KeyHandle(long id) { this.id = id; }
}
