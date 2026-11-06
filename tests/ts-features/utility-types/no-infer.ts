// @feature: `NoInfer<T>`
// @category: Utility Types
// @status: supported
// @note: `NoInfer<T>` is `T` (it only steers tsc's inference)
function createLight<C extends string>(colors: C[], defaultColor?: NoInfer<C>): string {
  return defaultColor ?? colors[0];
}
console.log(createLight(["red", "green"], "green"));
