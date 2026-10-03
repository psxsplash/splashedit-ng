-- Varargs in calls, returns, tables and select.
local function pack(...) return { n = select("#", ...), ... } end
local function first(...) local a = ... return a end
local function pass(...) return ... end
local function mid(a, ...) local b, c = ... return a, b, c, select(2, ...) end
local function count(...)
  local n = 0
  for _, v in ipairs({ ... }) do n = n + v end
  return n
end
local function tailcall(...) return pass(1, ...) end
local function wrap(f, ...) return (f(...)) end
local t = pack(1, nil, 3)
local s = string and string.format or nil
return t.n, first(9, 8), pass(), mid(1, 2, 3, 4), count(1, 2, 3), tailcall(2, 3),
  wrap(pass, 7, 8), { pass(1, 2) }, { pass(1, 2), 3 }, (pass(4, 5))
