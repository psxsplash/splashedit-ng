-- Constant folding with 32-bit integer numbers.
local a = 2147483647 + 1
local b = -2147483647 - 2
local c = 65536 * 65536
local d = 65536 * 32768
local e = 46341 * 46341
local f = 7 / 2
local g = -7 / 2
local h = 7 / -2
local i = -7 / -2
local j = 7 % 3
local k = -7 % 3
local l = 7 % -3
local m = -7 % -3
local n = 2147483648 / -1
local o = 2147483648 % -1
local p = 2 ^ 10
local q = 3 ^ 0
local r = (-2) ^ 3
local s = 2 ^ 31
local t = - -5
local u = -(2147483647 + 1)
local v = 1 / 0
local w = 5 % 0
local x = 0 / 0
local y = (1 + 2) * (3 + 4) - 10 / 3 % 2
local z = 100000 * 100000
return a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z
