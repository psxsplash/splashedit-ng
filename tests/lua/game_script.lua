-- A psxsplash-style object script.
local speed = 4096
local spin = 0
local hits = 0
local state = "idle"
local waypoints = { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } }
local current = 1

function onCreate(self)
  Debug.Log("created " .. Entity.GetName(self))
  local pos = Entity.GetPosition(self)
  self.home = { x = pos.x, y = pos.y, z = pos.z }
  Audio.Play(Audio.Find("spawn"), 100)
end

local function nearHome(self)
  local pos = Entity.GetPosition(self)
  local dx = pos.x - self.home.x
  local dz = pos.z - self.home.z
  return dx * dx + dz * dz < 1024 * 1024
end

function onUpdate(self, dt)
  spin = (spin + 64 * dt) % 4096
  Entity.SetRotationY(self, spin)
  if state == "idle" then
    if Input.IsPressedPlayer1(Input.CROSS) then
      state = "patrol"
      Camera.SetPosition(0, -2048, -8192)
    end
  elseif state == "patrol" then
    local wp = waypoints[current]
    local pos = Entity.GetPosition(self)
    local dx, dz = wp[1] - pos.x, wp[2] - pos.z
    if dx * dx + dz * dz < 256 then
      current = current % #waypoints + 1
    else
      Entity.SetPosition(self, { x = pos.x + dx / 16, y = pos.y, z = pos.z + dz / 16 })
    end
    if Input.IsPressedPlayer1(Input.CIRCLE) and nearHome(self) then state = "idle" end
  end
  local player = Player.GetPosition()
  local fwd = Camera.GetForward()
  if PSXMath.Abs(player.x - fwd.x) > speed then
    UI.SetText(UI.FindElement(UI.FindCanvas("hud"), "status"), "far: " .. hits)
  end
end

function onCollideWithPlayer(self)
  hits = hits + 1
  Entity.SetActive(self, hits < 3)
  if hits >= 3 then
    Scene.Load(Scene.GetIndex() + 1)
  end
end

function onTriggerEnter(self, other)
  Debug.Log("trigger " .. tostring(other))
  Debug.Log("frame " .. Timer.GetFrameCount())
end
