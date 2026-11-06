// @error: module 'st' has no exported member 'Counter'
import * as st from "./_lib/state.ts";
const n: st.Counter = 1;
console.log(n);
