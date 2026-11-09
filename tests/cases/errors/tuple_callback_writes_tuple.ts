// @error: this callback may write to the tuple
const t: [number, number] = [1, 2];
console.log(t.map((v) => { t[1] = 20; return v; }));
