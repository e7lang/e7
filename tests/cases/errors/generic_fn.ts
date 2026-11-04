// @error: generic functions are not supported
// @error: write one function per concrete type
function id<T>(x: T): T { return x; }
