-- Hex literals and numbers beyond 32 bits.
local h = { 0x0, 0x1, 0xff, 0XFF, 0xAbCd, 0x7fffffff, 0x80000000, 0xffffffff,
  0x100000000, 0x123456789, 0xdeadbeef }
local d = { 0, 1, 255, 2147483647, 2147483648, 2147483649, 4294967295,
  4294967296, 4294967297, 99999999999, 12345678901234567890 }
local lead = { 010, 00, 0007, -010 }
local neg = { -1, -2147483647, -2147483648, -2147483649, -4294967296 }
local big = 0xffffffff + 1
local wrap = 4294967295 * 4294967295
return h, d, lead, neg, big, wrap
