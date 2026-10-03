-- Long branch chains and large jump offsets.
local function classify(n)
  if n == 0 then return "c0"
  elseif n == 1 then return "c1"
  elseif n == 2 then return "c2"
  elseif n == 3 then return "c3"
  elseif n == 4 then return "c4"
  elseif n == 5 then return "c5"
  elseif n == 6 then return "c6"
  elseif n == 7 then return "c7"
  elseif n == 8 then return "c8"
  elseif n == 9 then return "c9"
  elseif n == 10 then return "c10"
  elseif n == 11 then return "c11"
  elseif n == 12 then return "c12"
  elseif n == 13 then return "c13"
  elseif n == 14 then return "c14"
  elseif n == 15 then return "c15"
  elseif n == 16 then return "c16"
  elseif n == 17 then return "c17"
  elseif n == 18 then return "c18"
  elseif n == 19 then return "c19"
  elseif n == 20 then return "c20"
  elseif n == 21 then return "c21"
  elseif n == 22 then return "c22"
  elseif n == 23 then return "c23"
  elseif n == 24 then return "c24"
  elseif n == 25 then return "c25"
  elseif n == 26 then return "c26"
  elseif n == 27 then return "c27"
  elseif n == 28 then return "c28"
  elseif n == 29 then return "c29"
  elseif n == 30 then return "c30"
  elseif n == 31 then return "c31"
  elseif n == 32 then return "c32"
  elseif n == 33 then return "c33"
  elseif n == 34 then return "c34"
  elseif n == 35 then return "c35"
  elseif n == 36 then return "c36"
  elseif n == 37 then return "c37"
  elseif n == 38 then return "c38"
  elseif n == 39 then return "c39"
  elseif n == 40 then return "c40"
  elseif n == 41 then return "c41"
  elseif n == 42 then return "c42"
  elseif n == 43 then return "c43"
  elseif n == 44 then return "c44"
  elseif n == 45 then return "c45"
  elseif n == 46 then return "c46"
  elseif n == 47 then return "c47"
  elseif n == 48 then return "c48"
  elseif n == 49 then return "c49"
  elseif n == 50 then return "c50"
  elseif n == 51 then return "c51"
  elseif n == 52 then return "c52"
  elseif n == 53 then return "c53"
  elseif n == 54 then return "c54"
  elseif n == 55 then return "c55"
  elseif n == 56 then return "c56"
  elseif n == 57 then return "c57"
  elseif n == 58 then return "c58"
  elseif n == 59 then return "c59"
  elseif n == 60 then return "c60"
  elseif n == 61 then return "c61"
  elseif n == 62 then return "c62"
  elseif n == 63 then return "c63"
  elseif n == 64 then return "c64"
  elseif n == 65 then return "c65"
  elseif n == 66 then return "c66"
  elseif n == 67 then return "c67"
  elseif n == 68 then return "c68"
  elseif n == 69 then return "c69"
  elseif n == 70 then return "c70"
  elseif n == 71 then return "c71"
  elseif n == 72 then return "c72"
  elseif n == 73 then return "c73"
  elseif n == 74 then return "c74"
  elseif n == 75 then return "c75"
  elseif n == 76 then return "c76"
  elseif n == 77 then return "c77"
  elseif n == 78 then return "c78"
  elseif n == 79 then return "c79"
  elseif n == 80 then return "c80"
  elseif n == 81 then return "c81"
  elseif n == 82 then return "c82"
  elseif n == 83 then return "c83"
  elseif n == 84 then return "c84"
  elseif n == 85 then return "c85"
  elseif n == 86 then return "c86"
  elseif n == 87 then return "c87"
  elseif n == 88 then return "c88"
  elseif n == 89 then return "c89"
  elseif n == 90 then return "c90"
  elseif n == 91 then return "c91"
  elseif n == 92 then return "c92"
  elseif n == 93 then return "c93"
  elseif n == 94 then return "c94"
  elseif n == 95 then return "c95"
  elseif n == 96 then return "c96"
  elseif n == 97 then return "c97"
  elseif n == 98 then return "c98"
  elseif n == 99 then return "c99"
  elseif n == 100 then return "c100"
  elseif n == 101 then return "c101"
  elseif n == 102 then return "c102"
  elseif n == 103 then return "c103"
  elseif n == 104 then return "c104"
  elseif n == 105 then return "c105"
  elseif n == 106 then return "c106"
  elseif n == 107 then return "c107"
  elseif n == 108 then return "c108"
  elseif n == 109 then return "c109"
  elseif n == 110 then return "c110"
  elseif n == 111 then return "c111"
  elseif n == 112 then return "c112"
  elseif n == 113 then return "c113"
  elseif n == 114 then return "c114"
  elseif n == 115 then return "c115"
  elseif n == 116 then return "c116"
  elseif n == 117 then return "c117"
  elseif n == 118 then return "c118"
  elseif n == 119 then return "c119"
  elseif n == 120 then return "c120"
  elseif n == 121 then return "c121"
  elseif n == 122 then return "c122"
  elseif n == 123 then return "c123"
  elseif n == 124 then return "c124"
  elseif n == 125 then return "c125"
  elseif n == 126 then return "c126"
  elseif n == 127 then return "c127"
  elseif n == 128 then return "c128"
  elseif n == 129 then return "c129"
  elseif n == 130 then return "c130"
  elseif n == 131 then return "c131"
  elseif n == 132 then return "c132"
  elseif n == 133 then return "c133"
  elseif n == 134 then return "c134"
  elseif n == 135 then return "c135"
  elseif n == 136 then return "c136"
  elseif n == 137 then return "c137"
  elseif n == 138 then return "c138"
  elseif n == 139 then return "c139"
  elseif n == 140 then return "c140"
  elseif n == 141 then return "c141"
  elseif n == 142 then return "c142"
  elseif n == 143 then return "c143"
  elseif n == 144 then return "c144"
  elseif n == 145 then return "c145"
  elseif n == 146 then return "c146"
  elseif n == 147 then return "c147"
  elseif n == 148 then return "c148"
  elseif n == 149 then return "c149"
  elseif n == 150 then return "c150"
  elseif n == 151 then return "c151"
  elseif n == 152 then return "c152"
  elseif n == 153 then return "c153"
  elseif n == 154 then return "c154"
  elseif n == 155 then return "c155"
  elseif n == 156 then return "c156"
  elseif n == 157 then return "c157"
  elseif n == 158 then return "c158"
  elseif n == 159 then return "c159"
  elseif n == 160 then return "c160"
  elseif n == 161 then return "c161"
  elseif n == 162 then return "c162"
  elseif n == 163 then return "c163"
  elseif n == 164 then return "c164"
  elseif n == 165 then return "c165"
  elseif n == 166 then return "c166"
  elseif n == 167 then return "c167"
  elseif n == 168 then return "c168"
  elseif n == 169 then return "c169"
  elseif n == 170 then return "c170"
  elseif n == 171 then return "c171"
  elseif n == 172 then return "c172"
  elseif n == 173 then return "c173"
  elseif n == 174 then return "c174"
  elseif n == 175 then return "c175"
  elseif n == 176 then return "c176"
  elseif n == 177 then return "c177"
  elseif n == 178 then return "c178"
  elseif n == 179 then return "c179"
  elseif n == 180 then return "c180"
  elseif n == 181 then return "c181"
  elseif n == 182 then return "c182"
  elseif n == 183 then return "c183"
  elseif n == 184 then return "c184"
  elseif n == 185 then return "c185"
  elseif n == 186 then return "c186"
  elseif n == 187 then return "c187"
  elseif n == 188 then return "c188"
  elseif n == 189 then return "c189"
  elseif n == 190 then return "c190"
  elseif n == 191 then return "c191"
  elseif n == 192 then return "c192"
  elseif n == 193 then return "c193"
  elseif n == 194 then return "c194"
  elseif n == 195 then return "c195"
  elseif n == 196 then return "c196"
  elseif n == 197 then return "c197"
  elseif n == 198 then return "c198"
  elseif n == 199 then return "c199"
  else return nil end
end
local acc = 0
for i = 1, 10 do
  if classify(i) then
    acc = acc + 0
    acc = acc + 1
    acc = acc + 2
    acc = acc + 3
    acc = acc + 4
    acc = acc + 5
    acc = acc + 6
    acc = acc + 7
    acc = acc + 8
    acc = acc + 9
    acc = acc + 10
    acc = acc + 11
    acc = acc + 12
    acc = acc + 13
    acc = acc + 14
    acc = acc + 15
    acc = acc + 16
    acc = acc + 17
    acc = acc + 18
    acc = acc + 19
    acc = acc + 20
    acc = acc + 21
    acc = acc + 22
    acc = acc + 23
    acc = acc + 24
    acc = acc + 25
    acc = acc + 26
    acc = acc + 27
    acc = acc + 28
    acc = acc + 29
    acc = acc + 30
    acc = acc + 31
    acc = acc + 32
    acc = acc + 33
    acc = acc + 34
    acc = acc + 35
    acc = acc + 36
    acc = acc + 37
    acc = acc + 38
    acc = acc + 39
    acc = acc + 40
    acc = acc + 41
    acc = acc + 42
    acc = acc + 43
    acc = acc + 44
    acc = acc + 45
    acc = acc + 46
    acc = acc + 47
    acc = acc + 48
    acc = acc + 49
    acc = acc + 50
    acc = acc + 51
    acc = acc + 52
    acc = acc + 53
    acc = acc + 54
    acc = acc + 55
    acc = acc + 56
    acc = acc + 57
    acc = acc + 58
    acc = acc + 59
    acc = acc + 60
    acc = acc + 61
    acc = acc + 62
    acc = acc + 63
    acc = acc + 64
    acc = acc + 65
    acc = acc + 66
    acc = acc + 67
    acc = acc + 68
    acc = acc + 69
    acc = acc + 70
    acc = acc + 71
    acc = acc + 72
    acc = acc + 73
    acc = acc + 74
    acc = acc + 75
    acc = acc + 76
    acc = acc + 77
    acc = acc + 78
    acc = acc + 79
    acc = acc + 80
    acc = acc + 81
    acc = acc + 82
    acc = acc + 83
    acc = acc + 84
    acc = acc + 85
    acc = acc + 86
    acc = acc + 87
    acc = acc + 88
    acc = acc + 89
    acc = acc + 90
    acc = acc + 91
    acc = acc + 92
    acc = acc + 93
    acc = acc + 94
    acc = acc + 95
    acc = acc + 96
    acc = acc + 97
    acc = acc + 98
    acc = acc + 99
    acc = acc + 100
    acc = acc + 101
    acc = acc + 102
    acc = acc + 103
    acc = acc + 104
    acc = acc + 105
    acc = acc + 106
    acc = acc + 107
    acc = acc + 108
    acc = acc + 109
    acc = acc + 110
    acc = acc + 111
    acc = acc + 112
    acc = acc + 113
    acc = acc + 114
    acc = acc + 115
    acc = acc + 116
    acc = acc + 117
    acc = acc + 118
    acc = acc + 119
    acc = acc + 120
    acc = acc + 121
    acc = acc + 122
    acc = acc + 123
    acc = acc + 124
    acc = acc + 125
    acc = acc + 126
    acc = acc + 127
    acc = acc + 128
    acc = acc + 129
    acc = acc + 130
    acc = acc + 131
    acc = acc + 132
    acc = acc + 133
    acc = acc + 134
    acc = acc + 135
    acc = acc + 136
    acc = acc + 137
    acc = acc + 138
    acc = acc + 139
    acc = acc + 140
    acc = acc + 141
    acc = acc + 142
    acc = acc + 143
    acc = acc + 144
    acc = acc + 145
    acc = acc + 146
    acc = acc + 147
    acc = acc + 148
    acc = acc + 149
    acc = acc + 150
    acc = acc + 151
    acc = acc + 152
    acc = acc + 153
    acc = acc + 154
    acc = acc + 155
    acc = acc + 156
    acc = acc + 157
    acc = acc + 158
    acc = acc + 159
    acc = acc + 160
    acc = acc + 161
    acc = acc + 162
    acc = acc + 163
    acc = acc + 164
    acc = acc + 165
    acc = acc + 166
    acc = acc + 167
    acc = acc + 168
    acc = acc + 169
    acc = acc + 170
    acc = acc + 171
    acc = acc + 172
    acc = acc + 173
    acc = acc + 174
    acc = acc + 175
    acc = acc + 176
    acc = acc + 177
    acc = acc + 178
    acc = acc + 179
    acc = acc + 180
    acc = acc + 181
    acc = acc + 182
    acc = acc + 183
    acc = acc + 184
    acc = acc + 185
    acc = acc + 186
    acc = acc + 187
    acc = acc + 188
    acc = acc + 189
    acc = acc + 190
    acc = acc + 191
    acc = acc + 192
    acc = acc + 193
    acc = acc + 194
    acc = acc + 195
    acc = acc + 196
    acc = acc + 197
    acc = acc + 198
    acc = acc + 199
    acc = acc + 200
    acc = acc + 201
    acc = acc + 202
    acc = acc + 203
    acc = acc + 204
    acc = acc + 205
    acc = acc + 206
    acc = acc + 207
    acc = acc + 208
    acc = acc + 209
    acc = acc + 210
    acc = acc + 211
    acc = acc + 212
    acc = acc + 213
    acc = acc + 214
    acc = acc + 215
    acc = acc + 216
    acc = acc + 217
    acc = acc + 218
    acc = acc + 219
    acc = acc + 220
    acc = acc + 221
    acc = acc + 222
    acc = acc + 223
    acc = acc + 224
    acc = acc + 225
    acc = acc + 226
    acc = acc + 227
    acc = acc + 228
    acc = acc + 229
    acc = acc + 230
    acc = acc + 231
    acc = acc + 232
    acc = acc + 233
    acc = acc + 234
    acc = acc + 235
    acc = acc + 236
    acc = acc + 237
    acc = acc + 238
    acc = acc + 239
    acc = acc + 240
    acc = acc + 241
    acc = acc + 242
    acc = acc + 243
    acc = acc + 244
    acc = acc + 245
    acc = acc + 246
    acc = acc + 247
    acc = acc + 248
    acc = acc + 249
    acc = acc + 250
    acc = acc + 251
    acc = acc + 252
    acc = acc + 253
    acc = acc + 254
    acc = acc + 255
    acc = acc + 256
    acc = acc + 257
    acc = acc + 258
    acc = acc + 259
    acc = acc + 260
    acc = acc + 261
    acc = acc + 262
    acc = acc + 263
    acc = acc + 264
    acc = acc + 265
    acc = acc + 266
    acc = acc + 267
    acc = acc + 268
    acc = acc + 269
    acc = acc + 270
    acc = acc + 271
    acc = acc + 272
    acc = acc + 273
    acc = acc + 274
    acc = acc + 275
    acc = acc + 276
    acc = acc + 277
    acc = acc + 278
    acc = acc + 279
    acc = acc + 280
    acc = acc + 281
    acc = acc + 282
    acc = acc + 283
    acc = acc + 284
    acc = acc + 285
    acc = acc + 286
    acc = acc + 287
    acc = acc + 288
    acc = acc + 289
    acc = acc + 290
    acc = acc + 291
    acc = acc + 292
    acc = acc + 293
    acc = acc + 294
    acc = acc + 295
    acc = acc + 296
    acc = acc + 297
    acc = acc + 298
    acc = acc + 299
    acc = acc + 300
    acc = acc + 301
    acc = acc + 302
    acc = acc + 303
    acc = acc + 304
    acc = acc + 305
    acc = acc + 306
    acc = acc + 307
    acc = acc + 308
    acc = acc + 309
    acc = acc + 310
    acc = acc + 311
    acc = acc + 312
    acc = acc + 313
    acc = acc + 314
    acc = acc + 315
    acc = acc + 316
    acc = acc + 317
    acc = acc + 318
    acc = acc + 319
    acc = acc + 320
    acc = acc + 321
    acc = acc + 322
    acc = acc + 323
    acc = acc + 324
    acc = acc + 325
    acc = acc + 326
    acc = acc + 327
    acc = acc + 328
    acc = acc + 329
    acc = acc + 330
    acc = acc + 331
    acc = acc + 332
    acc = acc + 333
    acc = acc + 334
    acc = acc + 335
    acc = acc + 336
    acc = acc + 337
    acc = acc + 338
    acc = acc + 339
    acc = acc + 340
    acc = acc + 341
    acc = acc + 342
    acc = acc + 343
    acc = acc + 344
    acc = acc + 345
    acc = acc + 346
    acc = acc + 347
    acc = acc + 348
    acc = acc + 349
    acc = acc + 350
    acc = acc + 351
    acc = acc + 352
    acc = acc + 353
    acc = acc + 354
    acc = acc + 355
    acc = acc + 356
    acc = acc + 357
    acc = acc + 358
    acc = acc + 359
    acc = acc + 360
    acc = acc + 361
    acc = acc + 362
    acc = acc + 363
    acc = acc + 364
    acc = acc + 365
    acc = acc + 366
    acc = acc + 367
    acc = acc + 368
    acc = acc + 369
    acc = acc + 370
    acc = acc + 371
    acc = acc + 372
    acc = acc + 373
    acc = acc + 374
    acc = acc + 375
    acc = acc + 376
    acc = acc + 377
    acc = acc + 378
    acc = acc + 379
    acc = acc + 380
    acc = acc + 381
    acc = acc + 382
    acc = acc + 383
    acc = acc + 384
    acc = acc + 385
    acc = acc + 386
    acc = acc + 387
    acc = acc + 388
    acc = acc + 389
    acc = acc + 390
    acc = acc + 391
    acc = acc + 392
    acc = acc + 393
    acc = acc + 394
    acc = acc + 395
    acc = acc + 396
    acc = acc + 397
    acc = acc + 398
    acc = acc + 399
  end
end
return acc
