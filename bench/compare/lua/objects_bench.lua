local Vec = {}
Vec.__index = Vec
function Vec.new(x, y, z) return setmetatable({ x = x, y = y, z = z }, Vec) end
function Vec:add(o) return Vec.new(self.x + o.x, self.y + o.y, self.z + o.z) end
function Vec:dot(o) return self.x * o.x + self.y * o.y + self.z * o.z end
local t0 = os.clock()
local acc = Vec.new(0, 0, 0)
local d = 0
for i = 0, 2000000 - 1 do local v = Vec.new(i, i * 0.5, 1); acc = acc:add(v); d = d + v:dot(acc) * 1e-12 end
print(math.floor(acc.x + 0.5), math.floor(d + 0.5), "objects", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
