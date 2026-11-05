// @feature: `static` initialization blocks
// @category: Classes
// @status: supported
class Config {
  static #secret = 0;
  static table: number[] = [];
  static {
    for (let i = 0; i < 4; i++) Config.table.push(i * i);
    Config.#secret = Config.table.length;
  }
  static secret(): number { return Config.#secret; }
}
console.log(Config.table, Config.secret());
