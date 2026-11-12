local t0 = os.clock()
local s = 0
for i = 0, 30000000 - 1 do s = s + i % 7 end
print(s, "loop", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
