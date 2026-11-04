export function gcd(a: number, b: number): number {
  while (b !== 0) { const t = a % b; a = b; b = t; }
  return a;
}
export function clamp(x: number, lo: number, hi: number): number {
  return x < lo ? lo : x > hi ? hi : x;
}
