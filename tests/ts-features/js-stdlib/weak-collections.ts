// @feature: `WeakMap` and `WeakSet`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet: the collector has no weak (ephemeron) tables
// @instead: `Map` / `Set` with explicit removal, or a field on the key object
// @error: 'WeakMap' is not supported in E7
class Key { id = 1; }
const meta = new WeakMap<Key, string>();
const k = new Key();
meta.set(k, "info");
console.log(meta.get(k));
