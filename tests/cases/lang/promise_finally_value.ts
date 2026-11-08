// finally keeps the value (of any type) and waits for a promise its callback returns
async function bad(): Promise<void> { throw new Error("from finally callback"); }
Promise.resolve(7).finally(() => {}).then((v) => console.log("after finally", v));
Promise.resolve(true).finally(() => {}).then((v) => console.log("bool", v));
Promise.resolve("v")
  .finally(() => bad())
  .then((v) => console.log("fulfilled", v), (e: Error) => console.log("rejected", e.message));
Promise.resolve().then(() => console.log("t1")).then(() => console.log("t2")).then(() => console.log("t3"));
async function main(): Promise<void> {
  const v = await Promise.resolve(42).finally(() => console.log("cleanup"));
  console.log("awaited", v);
}
main();
