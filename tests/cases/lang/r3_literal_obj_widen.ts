// third test round of 0.1.3 (literal_obj_widen): must print what Node.js prints
const N = "n";
const obj = { k: N };
obj.k = "zzz";
console.log(obj);
