-- Takes a screenshot at frame BOOT_FRAME, writes BOOT_OUT/shot.raw, prints its size, quits.
local outdir = os.getenv('BOOT_OUT')
local target = tonumber(os.getenv('BOOT_FRAME') or '600')
local frame = 0
function DrawImguiFrame()
  frame = frame + 1
  if frame == target then
    local ss = PCSX.GPU.takeScreenShot()
    local f = Support.File.open(outdir .. '/shot.raw', 'TRUNCATE')
    f:writeMoveSlice(ss.data); f:close()
    print('SHOT ' .. ss.width .. ' ' .. ss.height)
    PCSX.quit(0)
  end
end
