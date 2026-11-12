local t0 = os.clock()
local parts = {}
for i = 0, 200000 - 1 do parts[#parts + 1] = "k" .. i .. "=" .. (i * 7919) % 1000 end
local joined = table.concat(parts, "&")
local fields = {}
for f in joined:gmatch("[^&]+") do fields[#fields + 1] = f end
local m = {}
local keys = {}
for _, f in ipairs(fields) do
  local eq = f:find("=", 1, true)
  local k = f:sub(1, eq - 1)
  if m[k] == nil then keys[#keys + 1] = k end
  m[k] = f:sub(eq + 1)
end
table.sort(keys)
local acc = 0
for _, k in ipairs(keys) do acc = acc + #m[k] end
print(#fields, acc, "strings", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
