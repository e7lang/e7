// @error: must take no arguments and return an AsyncGenerator
class Bad {
  *[Symbol.asyncIterator](): Generator<number> {
    yield 1;
  }
}
async function main(): Promise<void> {
  for await (const x of new Bad()) console.log(x);
}
main();
