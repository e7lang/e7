// @feature: Call signatures in object types
// @category: More on Functions
// @status: unsupported
// @why: types known at compile time: functions are closures without properties
// @instead: an interface with a function-typed property, or a class with a method
// @error: call signatures in interfaces are not supported
type DescribableFunction = {
  description: string;
  (someArg: number): boolean;
};
function doSomething(fn: DescribableFunction): void {
  console.log(fn.description + " returned " + fn(6));
}
function myFunc(someArg: number): boolean { return someArg > 3; }
myFunc.description = "default description";
doSomething(myFunc);
