// @feature: Timers (`setTimeout`, `setInterval`, `queueMicrotask`)
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet: no event loop
// @instead: the host schedules calls to exported functions
// @error: cannot find name 'setTimeout'
setTimeout(() => console.log("later"), 10);
