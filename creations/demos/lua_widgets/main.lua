-- lua_widgets: build a panel + labels + two buttons ENTIRELY from Lua,
-- with a Lua onClick that fires on click (via WIDGET_LUA_DISPATCH), a
-- polling button read with IRGui.wasClicked from a Lua system, and a hover
-- label written by IRInput.onEntityHovered / onEntityUnhovered handlers (via
-- ENTITY_HOVER_DETECT). No per-creation C++ binding — every widget and handler
-- comes from the engine IRGui / IRInput surface.
--
-- Coords are in GUI-canvas trixels. The C++ GUI-test harness clicks at the
-- buttons' screen-px centers (see main_lua.cpp kOnClickEvents / kPollEvents).

local panel = IRGui.makePanel(40, 60, 560, 280, "LUA WIDGETS")
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

-- Paired value row: the slider owns no hidden value synchronization, but a
-- track-body double-click restores the pre-click slider value and moves
-- keyboard focus to this field on the following INPUT frame.
local pairedField = IRGui.makeNumberField(490, 302, 60, 24, 0.5, 0, 1, 0.1)
local pairedSlider = IRGui.makeSlider(
    340, 296, 140, 34, "PAIR", 0, 1, 0.5, nil,
    { default = 0.25, step = 0.1, fieldId = pairedField }
)
IRTest.setPairedValueRow(pairedSlider, pairedField)

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

-- Portrait: a secondary viewport pinned inside its own panel, viewing one world
-- cube through its own camera at zoom 16 while the world camera stays at 1.
-- The viewport is created, moved, resized, re-zoomed and retargeted from Lua.
local firstSubject = IRTest.spawnCube(-14, 2, 0, 3, 230, 90, 60)
local secondSubject = IRTest.spawnCube(2, -14, 0, 3, 60, 140, 230)
IRGui.makePanel(40, 340, 200, 300, "PORTRAIT")
local portrait = IRRender.createViewport({ x = 50, y = 370, width = 180, height = 260, zoom = 16 })
IRRender.setViewportSubject(portrait, firstSubject)
IRTest.setPortrait(portrait, firstSubject, secondSubject)

IRGui.makeButton(260, 500, 240, 100, "RETARGET", function(id)
    local x, y, width, height, zoom = 60, 380, 160, 240, 8
    IRRender.setViewportRect(portrait, x, y, width, height)
    IRRender.setViewportCamera(portrait, zoom, 0)
    IRRender.setViewportSubject(portrait, secondSubject)
    IRTest.onPortraitRetargeted(x, y, width, height, zoom)
end)

-- A selectable row: a label drawn by its own widget, and an invisible hit
-- region over the whole row that hovers and clicks as one unit. The region's
-- handlers repaint the label, so the label stays the visual indicator.
local ROW_TEXT = "ROW HIT REGION"
local rowLabel = IRGui.makeLabel(270, 422, ROW_TEXT)
local rowRegion = IRGui.makeHitRegion(260, 410, 330, 30, {
    onHover = function(id)
        IRGui.setLabelText(rowLabel, "> " .. ROW_TEXT)
        IRTest.rowHovered(id)
    end,
    onUnhover = function(id)
        IRGui.setLabelText(rowLabel, ROW_TEXT)
        IRTest.rowUnhovered(id)
    end,
    onClick = function(id)
        IRTest.rowClicked(id)
    end,
})
IRTest.setRowRegion(rowRegion)

-- A wheel-scrolled list: three visible labels over a window into SCROLL_ROWS
-- rows. IRInput.scrollDelta() is not hover-gated, so the panel gates it on its
-- own hit region: the window only moves while the pointer is over the region.
local SCROLL_ROWS, SCROLL_VISIBLE = 12, 3
local scrollTop, scrollHovered = 0, false
local scrollLabels = {}
local function scrollRowText(slot)
    return string.format("SCROLL ROW %02d", scrollTop + slot)
end
for slot = 1, SCROLL_VISIBLE do
    scrollLabels[slot] = IRGui.makeLabel(270, 326 + slot * 16, scrollRowText(slot))
end
IRGui.makeHitRegion(260, 335, 330, 70, {
    onHover = function() scrollHovered = true end,
    onUnhover = function() scrollHovered = false end,
})
IRTest.setScrollFirstRow(scrollLabels[1])

local C_ScrollTick = IRComponent.register("LuaWidgetsScrollTick", { n = 0 })
IREntity.singleton(C_ScrollTick)
IRSystem.appendSystem(IRTime.UPDATE, IRSystem.registerSystem({
    name = "LuaWidgetsScroll",
    components = { C_ScrollTick },
    tick = function()
        local steps = IRInput.scrollDelta()
        if not scrollHovered or steps == 0 then
            return
        end
        -- A positive step is the wheel rolling up: the window moves toward row 1.
        scrollTop = math.max(0, math.min(SCROLL_ROWS - SCROLL_VISIBLE, scrollTop - steps))
        for slot = 1, SCROLL_VISIBLE do
            IRGui.setLabelText(scrollLabels[slot], scrollRowText(slot))
        end
    end,
}))

local undoSliderStart = 0.25
local undoSlider = IRGui.makeSlider(80, 305, 240, 24, "UNDOABLE", 0, 1, undoSliderStart, IRText.Size.SMALL)
IRTest.setUndoSlider(undoSlider, undoSliderStart)
local undoSliderLast = undoSliderStart
local C_UndoSliderPoll = IRComponent.register("LuaWidgetsUndoSliderPoll", { n = 0 })
IREntity.singleton(C_UndoSliderPoll)
IRSystem.appendSystem(IRTime.UPDATE, IRSystem.registerSystem({
    name = "LuaWidgetsUndoSliderPoll",
    components = { C_UndoSliderPoll },
    tick = function()
        local current = IRGui.sliderValue(undoSlider)
        if current ~= undoSliderLast then
            local before, after = undoSliderLast, current
            IRUndo.push{
                label = "LUA SLIDER",
                coalesce = "lua_widgets.slider",
                undo = function()
                    IRGui.setSliderValue(undoSlider, before)
                    undoSliderLast = before
                end,
                redo = function()
                    IRGui.setSliderValue(undoSlider, after)
                    undoSliderLast = after
                end,
            }
            undoSliderLast = current
            IRTest.undoSliderChanged(current)
        end
    end,
}))

-- A 20-item dropdown: longer than the 8-row window an open dropdown shows, so
-- the wheel scrolls its items while the camera zoom stays put.
local dropdownItems = {}
for i = 1, 20 do
    dropdownItems[i] = string.format("OPTION %02d", i)
end
IRTest.setDropdown(IRGui.makeDropdown(510, 450, 120, 20, dropdownItems))

local hotkeyButton = IRGui.makeButton(20, 430, 180, 28, "HOTKEY BUTTON")
local hotkeyCheckbox = IRGui.makeCheckbox(20, 464, 180, 28, "HOTKEY CHECK", false)
local hotkeySlider = IRGui.makeSlider(20, 498, 180, 28, "HOTKEY SLIDER", 0, 10, 5, IRText.Size.SMALL, {
    default = 5,
    step = 1,
})
IRGui.setHotkey(hotkeyButton, { key = IRInput.Key.B, mods = IRInput.Modifier.ALT, hint = "B" })
IRGui.setHotkey(hotkeyCheckbox, { key = IRInput.Key.C, mods = IRInput.Modifier.ALT, hint = "C" })
IRGui.setHotkey(hotkeySlider, { key = IRInput.Key.S, mods = IRInput.Modifier.ALT, hint = "S" })

-- Hand-off fixture (--hand-off-fixture button|auto; absent, nothing below runs).
-- HAND OFF hands this process to a GUI-test take of the same demo and resumes
-- when it exits. The markers print the render frame and engine tick: equal
-- across a pair, the loop did not iterate while handed off.
local handOffMode = IRTest.handOffFixture()
if handOffMode ~= "off" then
    local frames = 0
    local function markers(phase)
        return "LUA_WIDGETS HANDOFF " .. phase .. " frame=" .. frames .. " tick=" .. IRTest.engineTick()
    end
    local function handOff()
        print(markers("before"))
        local r = IREngine.handOff{ argv = { "IRLuaWidgetsDemo", "--auto-screenshot", "3" } }
        print(markers("resumed") .. " ok=" .. tostring(r.ok) .. " supported=" .. tostring(r.supported) ..
              " exit=" .. tostring(r.exitCode) .. " verdict=" .. tostring(r.verdict) ..
              " error=" .. tostring(r.error))
        return r
    end
    IRGui.makeButton(260, 620, 240, 80, "HAND OFF", function() handOff() end)

    local C_HandOffClock = IRComponent.register("LuaWidgetsHandOffClock", { n = 0 })
    IREntity.singleton(C_HandOffClock)
    IRSystem.appendSystem(IRTime.RENDER, IRSystem.registerSystem({
        name = "LuaWidgetsHandOffFrames",
        components = { C_HandOffClock },
        tick = function() frames = frames + 1 end,
    }))
    if handOffMode == "auto" then
        local ticks, resumedAt = 0, nil
        IRSystem.appendSystem(IRTime.INPUT, IRSystem.registerSystem({
            name = "LuaWidgetsHandOffAuto",
            components = { C_HandOffClock },
            tick = function()
                ticks = ticks + 1
                if ticks == 30 then
                    handOff()
                    resumedAt = ticks
                elseif resumedAt ~= nil and ticks == resumedAt + 30 then
                    print(markers("closing"))
                    IRTest.closeWindow()
                end
            end,
        }))
    end
end
