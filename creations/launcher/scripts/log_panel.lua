-- Bottom zone: a 2000-line ring of tool output and launcher events, a
-- window of label rows over it, and the last hand-off result. The view
-- follows the tail until it is scrolled up; new lines then leave it where
-- it is.

local L = require("launcher_layout")

local M = {}

local RING = 2000
local SMALL = IRText.Size.SMALL

local function clip(text, cols)
    if #text <= cols then
        return text
    end
    return text:sub(1, cols - 3) .. "..."
end

local function maxScroll(log)
    return math.max(0, log.count - L.log.rows)
end

function M.new(app)
    local log = { lines = {}, head = 0, count = 0, scroll = 0, labels = {}, dirty = true }
    IRGui.makePanel(L.log.x, L.log.y, L.log.w, L.log.h, "LOG")
    log.result = IRGui.makeLabel(L.log.result.x, L.log.result.y, "LAST RUN: -", nil, SMALL)
    for row = 1, L.log.rows do
        log.labels[row] = IRGui.makeLabel(
            L.log.result.x, L.log.lineTop + (row - 1) * L.log.lineStep, "", nil, SMALL)
    end
    local half = math.floor(L.log.rows / 2)
    IRGui.makeButton(L.log.up.x, L.log.up.y, L.log.up.w, L.log.up.h, "UP", function()
        log.scroll = math.min(log.scroll + half, maxScroll(log))
        log.dirty = true
    end, SMALL)
    IRGui.makeButton(L.log.down.x, L.log.down.y, L.log.down.w, L.log.down.h, "DN", function()
        log.scroll = math.max(0, log.scroll - half)
        log.dirty = true
    end, SMALL)
    IRGui.makeButton(L.log.tail.x, L.log.tail.y, L.log.tail.w, L.log.tail.h, "TAIL", function()
        log.scroll = 0
        log.dirty = true
    end, SMALL)
    IRGui.makeButton(L.log.open.x, L.log.open.y, L.log.open.w, L.log.open.h, "OPEN LOG",
        function() app.openLogInEditor() end, SMALL)
    return log
end

function M.append(log, line)
    log.head = log.head % RING + 1
    log.lines[log.head] = line
    if log.count < RING then
        log.count = log.count + 1
    end
    if log.scroll > 0 then
        log.scroll = math.min(log.scroll + 1, maxScroll(log))
    end
    log.dirty = true
end

-- Line @p index of the ring, 1 being the oldest kept line.
function M.line(log, index)
    return log.lines[(log.head - log.count + index - 1) % RING + 1]
end

function M.text(log)
    local parts = {}
    for index = 1, log.count do
        parts[index] = M.line(log, index)
    end
    return table.concat(parts, "\n") .. "\n"
end

function M.setResult(log, text)
    IRGui.setLabelText(log.result, clip("LAST RUN: " .. text, L.log.cols))
end

function M.render(log)
    if not log.dirty then
        return
    end
    log.dirty = false
    local last = log.count - log.scroll
    local first = math.max(1, last - L.log.rows + 1)
    for row = 1, L.log.rows do
        local index = first + row - 1
        local text = ""
        if index <= last then
            text = clip(M.line(log, index), L.log.cols)
        end
        IRGui.setLabelText(log.labels[row], text)
    end
end

return M
