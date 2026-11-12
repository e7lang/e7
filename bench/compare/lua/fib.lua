local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
local t0 = os.clock()
local r = fib(30)
print(r, "fib", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
