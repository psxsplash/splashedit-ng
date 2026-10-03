-- Method calls, self, metatables and inheritance.
local Vec = {}
Vec.__index = Vec

function Vec.new(x, y) return setmetatable({ x = x, y = y }, Vec) end
function Vec:add(o) return Vec.new(self.x + o.x, self.y + o.y) end
function Vec:scale(k) self.x = self.x * k self.y = self.y * k return self end
function Vec:len2() return self.x * self.x + self.y * self.y end
Vec.__add = function(a, b) return a:add(b) end
Vec.__eq = function(a, b) return a.x == b.x and a.y == b.y end
Vec.__tostring = function(v) return "(" .. v.x .. "," .. v.y .. ")" end

local Vec3 = setmetatable({}, { __index = Vec })
Vec3.__index = Vec3
function Vec3.new(x, y, z)
  local v = Vec.new(x, y)
  v.z = z
  return setmetatable(v, Vec3)
end
function Vec3:len2() return Vec.len2(self) + self.z * self.z end

local a = Vec.new(1, 2)
local b = a:add(Vec.new(3, 4)):scale(2)
local c = Vec3.new(1, 2, 3)
local obj = { inner = { deep = { fn = function(self, v) return v end } } }
local r = obj.inner.deep:fn(5)
local s = ("abc"):upper()
local t = obj["inner"].deep["fn"](obj, 6)
return a + b, c:len2(), r, s, t, a == b
