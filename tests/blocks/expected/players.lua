local steps = 0

function onUpdate(self, dt)
    if Input.GetDevice(1) == "mouse" then
        steps = steps + (Input.GetMouse(1))
    end
    if Input.IsPressed(3, Input.CROSS) then
        Debug.Log("p3 y " .. select(2, Input.GetAnalog(3, 0)))
    end
    if (Input.GetDevice(1) == "mouse" and Input.IsReleased(1, Input.MOUSE_RIGHT)) then
        Debug.Log(select(2, Input.GetMouse(2)))
    end
    if Input.IsHeld(8, Input.SQUARE) then
        Debug.Log("held")
    end
end

function onButtonPress(self, button, byPlayer)
    if button == Input.CROSS then
        Debug.Log("anyone")

        if byPlayer == 5 then
            Debug.Log("player 5")
        end
    end
    if button == Input.MOUSE_LEFT and Input.GetDevice(byPlayer) == "mouse" then
        Debug.Log("click")
    end
end
