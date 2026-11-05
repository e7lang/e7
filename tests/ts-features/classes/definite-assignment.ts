// @feature: Definite assignment assertions (`x!: T`)
// @category: Classes
// @status: supported
// @note: an uninitialized field starts as `0` / `false` / `null` in E7 (`undefined` in JavaScript); the sample assigns it before reading
class Lazy {
  value!: number;
  init(v: number): void { this.value = v; }
}
let total!: number;
total = 10;
const l = new Lazy();
l.init(3);
console.log(l.value + total);
