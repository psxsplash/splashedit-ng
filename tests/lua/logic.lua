-- Boolean logic, comparisons, nil/true/false and zero constants.
local a, b, c = 1, nil, false
local x = a and b or c
local y = not a or (b and not c)
local z = (a == 1) and (b ~= nil) or (c == false)
local w = 1 < 2 and 3 <= 3 and 4 > 3 and 5 >= 5 and 6 ~= 7
local v = nil == false
local k0 = 0
local k1 = -0
local ks = "\0\0\0\0"
local kt = { [0] = "zero", ["\0\0\0\0"] = "string", [true] = 1, [false] = 0 }
local function test(p, q)
  if p and q then return 1
  elseif p or q then return 2
  elseif not (p or q) then return 3 end
  return 0
end
local cond = a > 0 and "pos" or a < 0 and "neg" or "zero"
local nested = ((a or b) and (c or a)) or ((b and c) or nil)
local cmp1, cmp2 = "abc" < "abd", "a" <= "a"
return x, y, z, w, v, k0, k1, ks, kt, test(true, nil), cond, nested
