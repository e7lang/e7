// @error: at most 65535 handlers
// finally blocks are copied for every way out of their try statements: deep nesting is bounded
function f(): number {
  let n = 0;
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  try { n++; if (n > 1000) return n; } finally {
  n++;
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  }
  return n;
}
console.log(f());
