-- Nested closures, shared and nested upvalues.
local function counter(start)
  local n = start
  local function inc(step) n = n + (step or 1) return n end
  local function get() return n end
  return inc, get
end

local function outer()
  local a, b = 1, 2
  return function()
    local c = a + b
    return function()
      local d = c * 2
      return function(x)
        a = a + 1
        return a + b + c + d + x
      end
    end
  end
end

local fns = {}
for i = 1, 5 do
  local j = i * 10
  fns[i] = function() return i + j end
end

local x = 0
while x < 3 do
  local captured = x
  fns[#fns + 1] = function() return captured end
  x = x + 1
end

local inc, get = counter(10)
inc() inc(5)
local deep = outer()()()(4)
local rec
rec = function(n) if n <= 0 then return 0 end return n + rec(n - 1) end
return get(), deep, fns, rec(10)
