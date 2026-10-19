-- Left zone: the catalog tree. Groups come from each manifest's slash-path
-- `group`; favourites float to a pinned group at the top and invalid or
-- manifest-less directories to a trailing Unsorted group, so nothing on disk
-- is hidden. The filter matches name, tags and description (the directory
-- for an Unsorted row) and keeps the matched rows' ancestors open.

local L = require("launcher_layout")

local M = {}

local SMALL = IRText.Size.SMALL
local FAVOURITES = "\1favourites"
local UNSORTED = "\1unsorted"

local function lower(text)
    return string.lower(text or "")
end

function M.displayName(creation)
    if creation.manifest then
        return creation.manifest.name
    end
    local tail = creation.dir:match("([^/]+)$") or creation.dir
    if creation.error then
        return tail .. " (INVALID)"
    end
    return tail .. " (NO MANIFEST)"
end

local function matches(creation, needle)
    if needle == "" then
        return true
    end
    local manifest = creation.manifest
    local haystack
    if manifest then
        haystack = manifest.name .. "\n" .. manifest.description .. "\n" ..
            table.concat(manifest.tags, " ")
    else
        haystack = creation.dir
    end
    return lower(haystack):find(needle, 1, true) ~= nil
end

local function newNode(name, path)
    return { name = name, path = path, groups = {}, groupNames = {}, creations = {} }
end

local function insert(root, creation)
    local node = root
    local group = creation.manifest.group or ""
    local path = ""
    for part in group:gmatch("[^/]+") do
        path = path == "" and part or (path .. "/" .. part)
        local child = node.groups[part]
        if not child then
            child = newNode(part, path)
            node.groups[part] = child
            node.groupNames[#node.groupNames + 1] = part
        end
        node = child
    end
    node.creations[#node.creations + 1] = creation
end

local function byName(a, b)
    return lower(M.displayName(a)) < lower(M.displayName(b))
end

local function countMatching(node, needle)
    local count = 0
    for _, creation in ipairs(node.creations) do
        if matches(creation, needle) then
            count = count + 1
        end
    end
    for _, name in ipairs(node.groupNames) do
        count = count + countMatching(node.groups[name], needle)
    end
    return count
end

-- Appends the visible rows of @p node's subtree at @p depth.
local function emit(view, rows, node, depth, needle)
    table.sort(node.groupNames)
    for _, name in ipairs(node.groupNames) do
        local child = node.groups[name]
        if countMatching(child, needle) > 0 then
            local collapsed = needle == "" and view.collapsed[child.path] == true
            rows[#rows + 1] = { kind = "group", name = name, path = child.path, depth = depth,
                                collapsed = collapsed }
            if not collapsed then
                emit(view, rows, child, depth + 1, needle)
            end
        end
    end
    table.sort(node.creations, byName)
    for _, creation in ipairs(node.creations) do
        if matches(creation, needle) then
            rows[#rows + 1] = { kind = "creation", creation = creation, depth = depth }
        end
    end
end

local function emitPinned(view, rows, name, path, creations, needle)
    local visible = {}
    for _, creation in ipairs(creations) do
        if matches(creation, needle) then
            visible[#visible + 1] = creation
        end
    end
    if #visible == 0 then
        return
    end
    table.sort(visible, byName)
    local collapsed = needle == "" and view.collapsed[path] == true
    rows[#rows + 1] = { kind = "group", name = name, path = path, depth = 0, collapsed = collapsed,
                        pinned = true }
    if not collapsed then
        for _, creation in ipairs(visible) do
            rows[#rows + 1] = { kind = "creation", creation = creation, depth = 1,
                                pinned = true }
        end
    end
end

local function buildRows(app, view)
    local root = newNode("", "")
    local unsorted, favourites = {}, {}
    for _, creation in ipairs(app.creations) do
        if creation.manifest then
            insert(root, creation)
        else
            unsorted[#unsorted + 1] = creation
        end
        if app.settings.favourites[creation.dir] then
            favourites[#favourites + 1] = creation
        end
    end
    local needle = lower(view.needle)
    local rows = {}
    emitPinned(view, rows, "FAVOURITES", FAVOURITES, favourites, needle)
    emit(view, rows, root, 0, needle)
    emitPinned(view, rows, "UNSORTED", UNSORTED, unsorted, needle)
    return rows
end

local function rowText(app, row)
    local indent = string.rep(" ", row.depth * 2)
    if row.kind == "group" then
        return indent .. (row.collapsed and "+ " or "- ") .. row.name
    end
    local marker = app.selectedDir == row.creation.dir and "> " or "  "
    return indent .. marker .. M.displayName(row.creation)
end

local function report(app, view)
    if not IRTest then
        return
    end
    local parts, count = {}, 0
    for index, row in ipairs(view.rows) do
        if row.kind == "group" then
            parts[index] = "G:" .. row.name
        else
            parts[index] = "C:" .. M.displayName(row.creation)
            if not row.pinned then
                count = count + 1
            end
        end
    end
    IRTest.report("tree", table.concat(parts, "|"))
    IRTest.report("treeCreations", tostring(count))
end

function M.new(app)
    local view = { rows = {}, widgets = {}, collapsed = {}, scroll = 0, needle = "" }
    IRGui.makePanel(L.tree.x, L.tree.y, L.tree.w, L.tree.h, "CREATIONS")
    local f = L.tree.filter
    view.filter = IRGui.makeTextInput(f.x, f.y, f.w, f.h, "", 40)
    IRGui.makeButton(L.tree.up.x, L.tree.up.y, L.tree.up.w, L.tree.up.h, "UP", function()
        view.scroll = math.max(0, view.scroll - L.tree.rows + 1)
        M.rebuild(app, view)
    end, SMALL)
    IRGui.makeButton(L.tree.down.x, L.tree.down.y, L.tree.down.w, L.tree.down.h, "DN", function()
        view.scroll = math.min(view.scroll + L.tree.rows - 1, math.max(0, #view.rows - L.tree.rows))
        M.rebuild(app, view)
    end, SMALL)
    view.countLabel = IRGui.makeLabel(L.tree.count.x, L.tree.count.y, "", nil, SMALL)
    return view
end

function M.clearFilter(app, view)
    IRGui.setTextInputValue(view.filter, "")
    view.needle = ""
    view.scroll = 0
    M.rebuild(app, view)
end

-- Rebuilds the tree when the filter text changed since the last frame.
function M.pollFilter(app, view)
    local text = IRGui.textInputValue(view.filter)
    if text ~= view.needle then
        view.needle = text
        view.scroll = 0
        M.rebuild(app, view)
    end
end

function M.rebuild(app, view)
    for _, id in ipairs(view.widgets) do
        IRGui.destroyWidget(id)
    end
    view.widgets = {}
    view.rows = buildRows(app, view)
    view.scroll = math.min(view.scroll, math.max(0, #view.rows - L.tree.rows))
    for slot = 1, L.tree.rows do
        local row = view.rows[view.scroll + slot]
        if not row then
            break
        end
        local y = L.tree.rowTop + (slot - 1) * L.tree.rowStep
        view.widgets[#view.widgets + 1] =
            IRGui.makeLabel(L.tree.x + L.tree.indent, y + 1, rowText(app, row), nil, SMALL)
        view.widgets[#view.widgets + 1] = IRGui.makeHitRegion(
            L.tree.x + 2, y, L.tree.w - 4, L.tree.rowStep, {
                onClick = function()
                    if row.kind == "group" then
                        view.collapsed[row.path] = not view.collapsed[row.path] or nil
                        M.rebuild(app, view)
                    else
                        app.select(row.creation.dir)
                    end
                end,
            })
    end
    local shown = math.min(#view.rows, L.tree.rows)
    IRGui.setLabelText(view.countLabel, string.format("%d-%d OF %d",
        #view.rows == 0 and 0 or view.scroll + 1, view.scroll + shown, #view.rows))
    report(app, view)
end

return M
