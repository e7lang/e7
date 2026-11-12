-- SHA-256 with Lua 5.3+ 64-bit integers, masked to 32 bits
local K = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
  0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f,
  0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2}
local M = 0xffffffff
local function rotr(x, n) return ((x >> n) | (x << (32 - n))) & M end
local function sha256(msg, len)   -- msg: 0-based table of bytes
  local H = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
  local total = math.ceil((len + 9) / 64) * 64
  local buf = {}
  for i = 0, len - 1 do buf[i] = msg[i] end
  for i = len, total - 1 do buf[i] = 0 end
  buf[len] = 0x80
  local bits = len * 8
  buf[total - 4] = (bits >> 24) & 255; buf[total - 3] = (bits >> 16) & 255; buf[total - 2] = (bits >> 8) & 255; buf[total - 1] = bits & 255
  local w = {}
  for off = 0, total - 1, 64 do
    for t = 0, 15 do
      local p = off + t * 4
      w[t] = buf[p] << 24 | buf[p + 1] << 16 | buf[p + 2] << 8 | buf[p + 3]
    end
    for t = 16, 63 do
      local x, y = w[t - 15], w[t - 2]
      local s0 = rotr(x, 7) ~ rotr(x, 18) ~ (x >> 3)
      local s1 = rotr(y, 17) ~ rotr(y, 19) ~ (y >> 10)
      w[t] = (w[t - 16] + s0 + w[t - 7] + s1) & M
    end
    local a, b, c, d, e, f, g, h = H[1], H[2], H[3], H[4], H[5], H[6], H[7], H[8]
    for t = 0, 63 do
      local S1 = rotr(e, 6) ~ rotr(e, 11) ~ rotr(e, 25)
      local ch = (e & f) ~ (~e & g)
      local t1 = (h + S1 + ch + K[t + 1] + w[t]) & M
      local S0 = rotr(a, 2) ~ rotr(a, 13) ~ rotr(a, 22)
      local maj = (a & b) ~ (a & c) ~ (b & c)
      local t2 = (S0 + maj) & M
      h = g; g = f; f = e; e = (d + t1) & M; d = c; c = b; b = a; a = (t1 + t2) & M
    end
    H[1] = (H[1] + a) & M; H[2] = (H[2] + b) & M; H[3] = (H[3] + c) & M; H[4] = (H[4] + d) & M
    H[5] = (H[5] + e) & M; H[6] = (H[6] + f) & M; H[7] = (H[7] + g) & M; H[8] = (H[8] + h) & M
  end
  local out = {}
  for i = 1, 8 do out[i] = string.format("%08x", H[i]) end
  return table.concat(out)
end
local n = 1 << 20
local data = {}
for i = 0, n - 1 do data[i] = i & 255 end
local t0 = os.clock()
local d = sha256(data, n)
print(d, "sha256 1MiB", math.floor((os.clock() - t0) * 1000 + 0.5), "ms")
