// @feature: `private` / `protected` constructors
// @category: Classes
// @status: supported
class Registry {
  static #instance: Registry | null = null;
  items: string[] = [];
  private constructor() {}
  static get(): Registry {
    const existing = Registry.#instance;
    if (existing !== null) return existing;
    const created = new Registry();
    Registry.#instance = created;
    return created;
  }
}
Registry.get().items.push("a");
Registry.get().items.push("b");
console.log(Registry.get().items);
