// The object and the index of an assignment target are evaluated before the right-hand side. When the
// right-hand side assigns to no variable they are used without a copy; these cases must still match JS.
class Box { v: number = 0; arr: number[] = [0, 0, 0, 0]; }
let gi = 0;
let garr: number[] = [0, 0, 0, 0];
function bumpG(): number { gi++; return 100 + gi; }
function swapG(): number { garr = [9, 9, 9, 9]; return 7; }

function locals(): string {
  const a: number[] = [0, 0, 0, 0, 0, 0];
  let i = 0;
  a[i] = 5;                 // no copy
  a[i + 1] = i + 10;        // no copy
  a[i] += 2;                // compound, no copy
  a[i] = i++;               // the right-hand side writes i: index 0 gets 0
  a[i] = ++i;               // index 1 gets 2
  a[i] = (i = 4);           // index 2 gets 4
  a[i] += (i = 5);          // index 4 += 5
  let b = a;
  const other: number[] = [1, 1, 1];
  b[0] = (b = other).length;   // the old array's slot 0 gets 3
  b[1] = b.length;             // no copy: other[1] = 3
  return JSON.stringify(a) + JSON.stringify(other) + " " + i;
}

function captured(): string {
  const a: number[] = [0, 0, 0, 0];
  let i = 0;
  const inc = (): number => { i++; return 50 + i; };
  a[i] = inc();             // i lives in a cell: index 0
  a[i] = inc();             // index 1
  a[i] += inc();            // index 2
  let arr = a;
  const repl = (): number => { arr = [8, 8]; return 1; };
  arr[3] = repl();          // the old array
  return JSON.stringify(a) + JSON.stringify(arr) + " " + i;
}

function members(): string {
  let box = new Box();
  const first = box;
  box.v = 1;                          // no copy
  box.v = (box = new Box()).v + 5;    // the old box gets 5
  box.arr[box.v] = 3;                 // no copy: new box, index 0
  box.arr[1] += box.arr[0];
  const gen = (): Box => { box = new Box(); return box; };
  box.v = gen().v + 9;                // the box before gen()
  return first.v + JSON.stringify(first.arr) + " " + box.v + JSON.stringify(box.arr);
}

function globals(): string {
  garr[gi] = bumpG();       // index 0 (gi is read before the call)
  garr[gi] = bumpG();       // index 1
  garr[gi] += bumpG();      // index 2
  const before = garr;
  garr[3] = swapG();        // the old array
  return JSON.stringify(before) + JSON.stringify(garr) + " " + gi;
}

function typed(): string {
  const u = new Uint8Array(4);
  const f = new Float64Array(3);
  let i = 0;
  u[i] = 200; u[i + 1] = u[i] + 100; u[i] += 1;
  u[i] = i++ + 3;
  f[i] = i / 2; f[i] *= 3; f[i + 1] = f[i]--;
  const t: [number, string] = [1, "a"];
  t[0] = t[0] + 1; t[1] = t[1] + "b";
  return JSON.stringify(Array.from(u)) + JSON.stringify(Array.from(f)) + JSON.stringify(t) + " " + i;
}

console.log(locals());
console.log(captured());
console.log(members());
console.log(globals());
console.log(typed());
