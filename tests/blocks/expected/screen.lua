-- Blocks that wait run as tasks (coroutines); onUpdate resumes them every frame.
local tasks = {}

local function resumeTask(name, ...)
    local ok, err = coroutine.resume(tasks[name], ...)
    if coroutine.status(tasks[name]) == "dead" then
        tasks[name] = nil
    end
    if not ok then
        error(err, 0)
    end
end

-- Starts fn as the task called name, unless that task is still waiting.
local function startTask(name, fn)
    if tasks[name] == nil then
        tasks[name] = coroutine.create(fn)
        resumeTask(name)
    end
end

-- dt counts 4096 per 30fps frame, so a second is 30 * 4096. Always lets a frame pass.
local function wait(seconds)
    local left
    if type(seconds) == "table" then
        left = seconds._raw * 30  -- a FixedPoint keeps 4096 per unit in _raw
    else
        left = seconds * 122880
    end
    repeat
        left = left - coroutine.yield()
    until left <= 0
end

function onUpdate(self, dt)
    if tasks.onInteract then
        resumeTask("onInteract", dt)
    end
end

function onCollideWithPlayer(self)
    Screen.Flash(255, 255, 255, 10)
    Screen.Shake(8, 15)
end

function onInteract(self)
    startTask("onInteract", function()
        Screen.FadeOut(255, 32, 0, 30)
        while not not Screen.IsFading() do
            coroutine.yield()
        end
        Screen.Letterbox(24, 15)
        Screen.Trail(1)
        Screen.FadeIn(15)
        wait(2)
        Screen.Trail(0)
        Screen.Letterbox(0, 15)
    end)
end
