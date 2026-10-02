-- lua_widgets: build a panel + labels + two buttons ENTIRELY from Lua,
-- with a Lua onClick that fires on click (via WIDGET_LUA_DISPATCH), a
-- polling button read with IRGui.wasClicked from a Lua system, and a hover
-- label written by IRInput.onEntityHovered / onEntityUnhovered handlers (via
-- ENTITY_HOVER_DETECT). No per-creation C++ binding — every widget and handler
-- comes from the engine IRGui / IRInput surface.
--
-- Coords are in GUI-canvas trixels. The C++ GUI-test harness clicks at the
-- buttons' screen-px centers (see main_lua.cpp kOnClickEvents / kPollEvents).

local panel = IRGui.makePanel(40, 60, 560, 260, "LUA WIDGETS")
local title = IRGui.makeLabel(60, 92, "BUILT ENTIRELY FROM LUA")

-- Button A — a Lua onClick fires on click (the new WIDGET_LUA_DISPATCH path).
local clickCount = 0
local onClickButton = IRGui.makeButton(80, 140, 240, 100, "CLICK ME", function(id)
    clickCount = clickCount + 1
    -- wasClicked(id) reads the same fireAction_ the dispatch just consumed, so
    -- it is true inside the handler — exercises the polling binding too.
    print("LUA_WIDGETS onClick fired id=" .. id ..
          " wasClicked=" .. tostring(IRGui.wasClicked(id)) ..
          " count=" .. clickCount)
    IRTest.onClickFired()
end)

-- Button B — NO onClick; proven via a Lua system that polls IRGui.wasClicked.
local pollButton = IRGui.makeButton(340, 140, 200, 100, "POLL ME")

-- Hover label — names the button under the cursor, empty when none is.
local hoverLabel = IRGui.makeLabel(60, 280, "")
local hoverNames = { [onClickButton] = "CLICK ME", [pollButton] = "POLL ME" }
IRInput.onEntityHovered(function(id)
    local wx, wy, wz = IRInput.mouseWorldPosAt({ 7, -3, 2 })
    local sx, sy = IRInput.mouseIsoScreen()
    IRTest.cursorPosition(wx, wy, wz, sx, sy, 7, -3, 2)
    IRGui.setLabelText(hoverLabel, "HOVERING " .. (hoverNames[id] or "?"))
end)
IRInput.onEntityUnhovered(function()
    IRGui.setLabelText(hoverLabel, "")
end)

-- Hand the widget ids back to the C++ GUI-test harness for its assertions.
IRTest.setButtons(onClickButton, pollButton, hoverLabel)

-- A pure-polling Lua creation: a singleton-backed system that ticks once per
-- frame and polls wasClicked on the poll button — no onClick callback.
local C_PollTick = IRComponent.register("LuaWidgetsPollTick", { n = 0 })
IREntity.singleton(C_PollTick)
local pollSys = IRSystem.registerSystem({
    name = "LuaWidgetsPoll",
    components = { C_PollTick },
    tick = function(arch)
        if IRGui.wasClicked(pollButton) then
            print("LUA_WIDGETS poll observed wasClicked button=" .. pollButton)
            IRTest.onPollFired()
        end
    end,
})
IRSystem.appendSystem(IRTime.UPDATE, pollSys)

-- Layout sanity: the GUI canvas size is reachable from Lua.
local gw, gh = IRRender.getGuiCanvasSize()
local sx, sy = IRGui.glyphStep()
print("LUA_WIDGETS gui canvas=" .. gw .. "x" .. gh .. " glyphStep=" .. sx .. "x" .. sy)
