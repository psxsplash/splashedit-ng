-- On the crate: counts updates and, at update 240, stops the Fountain emitter
-- from Lua and plays a fresh one-shot Sparks burst. Logs how many Fountain
-- particles were alive then and, 120 updates later (Fountain's particles live
-- 1.5 s), how many are left (0 expected) and whether it still plays.
local n = 0
function onUpdate(self, dt)
    n = n + 1
    if n == 240 then
        Debug.Log("PARTICLES STOP at update " .. n .. " live " .. Particles.Count("Fountain"))
        Particles.Stop("Fountain")
        Particles.Play("Sparks")
    elseif n == 360 then
        Debug.Log("PARTICLES AFTER STOP live " .. Particles.Count("Fountain") .. " playing " ..
                  tostring(Particles.IsPlaying("Fountain")))
    end
end
