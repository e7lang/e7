// @feature: `NoInfer<T>`
// @category: Utility Types
// @status: unsupported
// @why: not implemented yet
// @instead: an explicit type argument at the call
// @error: unknown type 'NoInfer'
function createLight<C extends string>(colors: C[], defaultColor?: NoInfer<C>): string {
  return defaultColor ?? colors[0];
}
console.log(createLight(["red", "green"], "green"));
