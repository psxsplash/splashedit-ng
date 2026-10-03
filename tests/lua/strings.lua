-- Short strings: empty, escapes, embedded zeros.
local s0 = ""
local s1 = "a"
local s2 = 'single \'quoted\' "string"'
local s3 = "tab\tnewline\ncr\rbell\abs\bff\fvt\vbackslash\\quote\""
local s4 = "zero\0inside\0\0end"
local s5 = "\0"
local s6 = "dec \65\066\0677 \255\1\01\001"
local s7 = "hex \x41\x7a\xff\x00\x7F"
local s8 = "skip \z
            whitespace"
local s9 = "line\
continued"
local s10 = "\0\0\0\0"
local t = { [""] = 1, ["\0"] = 2, ["a\0b"] = 3 }
return s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, t, #s4
