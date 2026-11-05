// @feature: `WeakRef` and `FinalizationRegistry`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: JavaScript-identical behaviour: their results depend on when the garbage collector runs
// @instead: explicit ownership: remove entries when the owner is done with them
// @error: cannot find class 'WeakRef'
class Session { id = 1; }
const ref = new WeakRef(new Session());
const registry = new FinalizationRegistry((id: number) => console.log("collected", id));
registry.register(new Session(), 2);
console.log(ref.deref()?.id);
