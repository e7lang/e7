try {
  try { decodeURIComponent("%"); } catch (e) { throw new Error("wrapped", { cause: e }); }
} catch (e) {
  if (e instanceof Error) {
    console.log(e.message, e.cause instanceof Error, String(e.cause));
  }
}
const plain = new Error("p");
console.log(plain.cause === undefined);
const ag = new AggregateError([new Error("a"), new TypeError("b")], "many");
console.log(ag.name, ag.message, ag.errors.length, ag.errors.map(x => x.message), String(ag), ag instanceof Error);
Promise.any([Promise.reject(new Error("x")), Promise.reject(new Error("y"))]).catch(e => {
  if (e instanceof AggregateError) console.log(e.message, e.errors.map(x => x.message));
});
