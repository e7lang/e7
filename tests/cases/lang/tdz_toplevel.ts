// a top-level let / const read or written by a function before its declaration ran: ReferenceError
function readY(): number { return y * 2; }
function setZ(): void { z = 5; }
try { console.log(readY()); } catch (e) { console.log(String(e)); }
try { setZ(); } catch (e) { console.log(String(e)); }
let y = 21;
let z = 0;
const [p, q] = [1, 2];
function pq(): number { return p + q; }
console.log(readY(), pq());
setZ();
console.log(z);
