function h(a: number, b: number = a): number {
  return [1, 2].map((x) => x + a).length + b;
}
console.log(h(1), h(1, 5));

function g(a: number, f: () => number = () => a * 10, c: number = f() + a): number {
  a += 1;
  return f() + c;
}
console.log(g(2), g(2, () => 7), g(2, () => 7, 1));

class K {
  n: number = 3;
  m(a: number, b: number = this.n + a): number {
    const add = (x: number): number => x + this.n + a + b;
    return add(1);
  }
}
console.log(new K().m(1), new K().m(1, 100));

const arrow = (a: number, b: number = a * 2, c: string = `${a}/${b}`): string => {
  const f = (): string => c + a + b;
  return f();
};
console.log(arrow(1), arrow(1, undefined, "z"), arrow(4, 5));

function opt(s: string, t?: string, u: string = t ?? s): string {
  const k = (): string => s + (t ?? "-") + u;
  return k();
}
console.log(opt("a"), opt("a", "b"), opt("a", "b", "c"));
