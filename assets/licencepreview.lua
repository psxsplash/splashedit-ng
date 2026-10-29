-- SplashEdit boot screen preview, loaded into pcsx-redux by the Disc
-- section's Preview boot screen (editor::licencePreviewCommand, -dofile).
--
-- Redux boots the real BIOS on a disc holding the licence alone. At vsync
-- kFrame this takes one screenshot into preview.raw (the display area as the
-- GPU holds it: 16-bit 1555 words, or 24-bit RGB), then writes preview.txt
-- ("<width> <height> <bpp>"), last, so a preview.txt means the raw is whole,
-- and quits. Both land in the directory the editor passes as -pcdrvbase
-- (PCdrv itself stays off). A failure is written to preview.err instead.

-- Measured on SCPH-5501 (redux HEAD, licence-only disc, vsyncs counted from
-- power on): vsync 800 the PlayStation wordmark still fading in, 820 nearly
-- there, 822 to 4000 (every 2 to 860, every 20 to 1200, every 100 to 4000)
-- the licence screen, logo at rest, identical captures; with no SYSTEM.CNF
-- the BIOS stays on it. 1200 sits well inside that hold. Provisional for
-- other BIOS versions, whose timing has not been measured.
local kFrame = 1200

local function dir()
    local ok, d = pcall(function() return PCSX.settings.emulator.Debug.PCdrvBase end)
    if not ok or type(d) ~= 'string' or d == '' then return nil end
    local last = d:sub(-1)
    if last ~= '/' and last ~= '\\' then d = d .. '/' end
    return d
end

-- As fast as the host allows (Speed 0 or less: unbounded, a redux setting
-- older builds lack, which then run in real time): about 3 s instead of 20.
pcall(function() PCSX.settings.spu.Speed = 0 end)

local vsyncs = 0
SplashEditLicencePreview = PCSX.Events.createEventListener('GPU::Vsync', function()
    vsyncs = vsyncs + 1
    if vsyncs ~= kFrame then return end
    local out = dir()
    local ok, err = pcall(function()
        if not out then error('no output directory (-pcdrvbase)') end
        local ss = PCSX.GPU.takeScreenShot()
        local f = Support.File.open(out .. 'preview.raw', 'TRUNCATE')
        f:writeMoveSlice(ss.data)
        f:close()
        local t = assert(io.open(out .. 'preview.txt', 'wb'))
        t:write(ss.width .. ' ' .. ss.height .. ' ' .. (tonumber(ss.bpp) == 1 and 24 or 16) .. '\n')
        t:close()
        print('licencepreview: ' .. ss.width .. 'x' .. ss.height .. ' at vsync ' .. vsyncs)
    end)
    if not ok then
        print('licencepreview: ' .. tostring(err))
        if out then
            local e = io.open(out .. 'preview.err', 'wb')
            if e then
                e:write(tostring(err))
                e:close()
            end
        end
    end
    PCSX.quit(ok and 0 or 1)
end)
