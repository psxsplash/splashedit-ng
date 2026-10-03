-- goto, labels, continue-style loops, nested blocks.
local out = {}
for i = 1, 10 do
  if i % 2 == 0 then goto continue end
  out[#out + 1] = i
  ::continue::
end

local n = 0
::top::
n = n + 1
if n < 5 then goto top end

do
  local k = 1
  ::again::
  k = k * 2
  if k < 100 then
    local captured = k
    out[#out + 1] = function() return captured end
    goto again
  end
end

for i = 1, 3 do
  for j = 1, 3 do
    if j == 2 then goto next_i end
    out[#out + 1] = i * 10 + j
  end
  ::next_i::
end

goto done
out = nil
::done::
return out, n
