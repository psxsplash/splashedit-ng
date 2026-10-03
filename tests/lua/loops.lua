-- Numeric and generic for, while, repeat, break.
local sum = 0
for i = 1, 100 do sum = sum + i end
for i = 100, 1, -3 do sum = sum - i end
for i = -5, 5, 2 do sum = sum + i * i end
local step = 4
for i = 0, 1000, step do if i > 50 then break end sum = sum + i end
for i = 2147483640, 2147483647 do sum = sum + 1 end

local t = { 10, 20, 30, a = 1, b = 2 }
for i, v in ipairs(t) do sum = sum + i * v end
for k, v in pairs(t) do if type(k) == "string" then sum = sum + v end end
local function iter(s, i) if i < s then return i + 1, i * i end end
for i, sq in iter, 5, 0 do sum = sum + sq end

local n = 10
while n > 0 do n = n - 1 if n == 3 then break end end
repeat
  local m = n * 2
  n = n + 1
until m > 20
local nested = 0
for i = 1, 4 do
  for j = i, 4 do
    for k = j, 4 do nested = nested + i * j * k end
  end
end
return sum, n, nested
