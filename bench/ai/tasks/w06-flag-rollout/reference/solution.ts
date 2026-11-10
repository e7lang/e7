import { utf8Encode } from "e7:encoding";

export interface Flag { key: string; enabled: boolean; rolloutPercent: number; allowUsers: string[]; denyUsers: string[] }
export interface Variant { name: string; weight: number }

export function fnv1a32(text: string): number {
  let h: u32 = 0x811c9dc5;
  for (const b of utf8Encode(text)) {
    h ^= u32(b);
    h *= 16777619;
  }
  return Number(h);
}

function hashFor(key: string, userId: string): number {
  return fnv1a32(`${key}:${userId}`);
}

export function bucketOf(flagKey: string, userId: string): number {
  return hashFor(flagKey, userId) % 10000;
}

export function isEnabled(flag: Flag, userId: string): boolean {
  if (!flag.enabled) return false;
  if (flag.denyUsers.includes(userId)) return false;
  if (flag.allowUsers.includes(userId)) return true;
  return bucketOf(flag.key, userId) < Math.round(flag.rolloutPercent * 100);
}

export function variantFor(experimentKey: string, userId: string, variants: Variant[]): string {
  const total = variants.reduce((sum, v) => sum + v.weight, 0);
  if (total <= 0) throw new Error("no variants");
  const point = hashFor(experimentKey, userId) % total;
  let upTo = 0;
  for (const v of variants) {
    upTo += v.weight;
    if (point < upTo) return v.name;
  }
  throw new Error("unreachable");
}
