// @error: which a 'number' cannot hold in e7
async function load(): Promise<number> { throw new Error("x"); }
async function main(): Promise<void> {
  const v = await load().catch(() => {});
  console.log(v);
}
main();
