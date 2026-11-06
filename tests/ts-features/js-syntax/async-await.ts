// @feature: `async` functions and `await`
// @category: JavaScript: Syntax
// @status: supported
// @note: async functions, methods and arrow functions; host functions return promises the host settles later (`declare function f(): Promise<T>`, see EMBEDDING.md)
async function load(): Promise<string> {
  return "data";
}
async function main(): Promise<void> {
  console.log(await load());
  try {
    await Promise.reject(new Error("failed"));
  } catch (e) {
    if (e instanceof Error) console.log("caught", e.message);
  }
}
main();
