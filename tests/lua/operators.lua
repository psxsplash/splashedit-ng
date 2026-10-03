-- Concat chains, length, unary minus, string arithmetic and precedence.
local name = "player"
local id = 42
local s = "id=" .. id .. " name=" .. name .. " x" .. 1 .. 2 .. 3
local len = #s + #"literal" + #{ 1, 2, 3 }
local neg = -id
local negs = -"5"
local sarith = "10" + 1
local prec = 2 + 3 * 4 ^ 2 / 8 - -1 % 5
local right = 2 ^ 2 ^ 2
local cat = "a" .. "b" .. "c" .. "d" .. "e" .. "f" .. "g" .. "h"
local mix = 1 .. ""
local paren = (((((1)))))
local t = {}
t.a = { b = { c = {} } }
t.a.b.c.d = id * 2 + #t
t["x" .. id] = s:sub(1, 3)
return s, len, neg, negs, sarith, prec, right, cat, mix, paren, t
