// `import type` and `export type ... from` are erased: a module reached only through them is not run; names
// imported that way are types only. A namespace import of e7:crypto names KeyHandle under any alias.
import type { Shape, Kind } from "./_lib/typeonly_types.ts";
import { type Point, type Box } from "./_lib/typeonly_hub.ts";
import * as c from "e7:crypto";
function label(s: Shape, k: Kind, p: Point): string { return `${s.name}:${k}:${p.x + p.y}`; }
function size(b: Box | null): number { return b === null ? 0 : b.w; }
export function mac(key: c.KeyHandle, data: Uint8Array): number { return c.hmac("sha256", key, data).length; }
console.log(typeof label, size(null), typeof mac);
