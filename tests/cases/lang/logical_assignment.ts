// ||= and &&= on variables, properties, elements and nullable targets (narrowed after ||= with a non-null value)
let a = 0; a ||= 5;
let b = 3; b ||= 7;
let c = 2; c &&= 9;
let d = 0; d &&= 9;
let s = ""; s ||= "def";
let t = "x"; t &&= t + "y";
let n: string | null = null; n ||= "filled";
const o = { count: 0, name: "", flags: [1] };
o.count ||= 10; o.name &&= "never"; o.flags &&= [2, 3];
const arr = [0, 1, 0];
for (let i = 0; i < arr.length; i++) arr[i] ||= 100 + i;
let bb = false; bb ||= true;
let bi = 0n; bi ||= 5n;
let calls = 0;
function side(): number { calls++; return 4; }
let z = 1; z ||= side();
console.log(a, b, c, d, s, t, n, o, arr, bb, bi, z, calls, (a ||= 1), (d &&= 2));
function f(n: string | null): number { n ||= "x"; return n.length; }
console.log(f(null), f("abc"), f(""));
