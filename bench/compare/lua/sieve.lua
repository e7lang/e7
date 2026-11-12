local t0 = os.clock()
local N = 5000000
local flags = {}
for i = 0, N do flags[i] = true end
local count = 0
for i = 2, N do
  if flags[i] then count = count + 1; for j = i * 2, N, i do flags[j] = false end end
end
print(count, "sieve", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
