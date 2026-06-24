-- lua_widgets (#1975): build a panel + label + two buttons ENTIRELY from Lua,
-- with a Lua onClick that fires on click (via WIDGET_LUA_DISPATCH) and a
-- polling button read with IRGui.wasClicked from a Lua system. No per-creation
-- C++ widget binding — every widget comes from the engine IRGui surface.
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

-- Hand the widget ids back to the C++ GUI-test harness for its assertions.
IRTest.setButtons(onClickButton, pollButton)

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
