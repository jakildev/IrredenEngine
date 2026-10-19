-- Center zone: the selected creation. Name, description, tags, docs links,
-- Open in IDE / Open folder, and one row per target with its built state,
-- last verdict, preset dropdown, Build and Run. Every widget here is rebuilt
-- on selection, so the zone holds nothing between selections but its panel.

local L = require("launcher_layout")
local catalogView = require("catalog_view")

local M = {}

local SMALL = IRText.Size.SMALL

local function wrap(text, cols, maxLines)
    local lines, line = {}, ""
    for word in (text or ""):gmatch("%S+") do
        if line == "" then
            line = word
        elseif #line + 1 + #word <= cols then
            line = line .. " " .. word
        else
            lines[#lines + 1] = line
            line = word
        end
    end
    if line ~= "" then
        lines[#lines + 1] = line
    end
    if #lines > maxLines then
        lines[maxLines] = lines[maxLines]:sub(1, cols - 3) .. "..."
        for index = #lines, maxLines + 1, -1 do
            lines[index] = nil
        end
    end
    return lines
end

local function clip(text, cols)
    if #text <= cols then
        return text
    end
    return "..." .. text:sub(#text - cols + 4)
end

local function builtText(target)
    if not target.built then
        return "NOT BUILT"
    end
    return "BUILT " .. IRLauncher.formatTime(target.mtime)
end

local function verdictText(target)
    local run = target.lastRun
    if not run then
        return "NO RUN RECORDED"
    end
    local monthDay, minute = (run.at or ""):match("^%d+%-(%d+%-%d+)T(%d+:%d+)")
    return "LAST " .. run.verdict .. (monthDay and (" " .. monthDay .. " " .. minute) or "")
end

function M.new(app)
    local details = { widgets = {}, presets = {}, page = 0 }
    IRGui.makePanel(L.details.x, L.details.y, L.details.w, L.details.h, "DETAILS")
    IRGui.makePanel(L.assets.x, L.assets.y, L.assets.w, L.assets.h, "ASSETS")
    IRGui.makeLabel(L.assets.x + 8, L.assets.y + 20, "ASSET PREVIEWS", nil, SMALL)
    IRGui.makeLabel(L.assets.x + 8, L.assets.y + 30, "ARRIVE IN A LATER", nil, SMALL)
    IRGui.makeLabel(L.assets.x + 8, L.assets.y + 40, "SLICE", nil, SMALL)
    return details
end

local function add(details, id)
    details.widgets[#details.widgets + 1] = id
    return id
end

function M.clear(details)
    for _, id in ipairs(details.widgets) do
        IRGui.destroyWidget(id)
    end
    details.widgets = {}
    details.presets = {}
end

local function showTargets(app, details, creation)
    local targets = creation.targets
    local manifestTargets = creation.manifest.targets
    local perPage = L.details.targets
    local first = details.page * perPage + 1
    for slot = 1, perPage do
        local index = first + slot - 1
        local target = targets[index]
        if not target then
            break
        end
        local y = L.details.targetTop + (slot - 1) * L.details.targetStep
        local display = target.display ~= "" and target.display or target.name
        add(details, IRGui.makeLabel(L.details.left, y, display, nil, SMALL))
        add(details, IRGui.makeLabel(L.details.left, y + 12, builtText(target), nil, SMALL))
        add(details, IRGui.makeLabel(L.details.left, y + 24, verdictText(target), nil, SMALL))

        local presets = manifestTargets[index].presets
        local names = {}
        for presetIndex = 1, #presets do
            names[presetIndex] = presets[presetIndex].name
        end
        local selected = app.presetIndex(target.name, #names)
        local p = L.details.preset
        if #names > 0 then
            local dropdown = add(details,
                IRGui.makeDropdown(p.x, y, p.w, p.h, names, selected, p.h, SMALL))
            details.presets[#details.presets + 1] = {
                dropdown = dropdown, target = target.name, selected = selected,
            }
        end
        local b, r = L.details.build, L.details.run
        add(details, IRGui.makeButton(b.x, y + b.dy, b.w, b.h, "BUILD", function()
            app.selectTarget(index)
            app.build(target.name)
        end, SMALL))
        add(details, IRGui.makeButton(r.x, y + r.dy, r.w, r.h, "RUN", function()
            app.selectTarget(index)
            app.run(index)
        end, SMALL))
    end
    if #targets > perPage then
        local m = L.details.more
        local pages = math.ceil(#targets / perPage)
        add(details, IRGui.makeLabel(L.details.left, m.y + 4,
            string.format("TARGETS %d-%d OF %d", first, math.min(first + perPage - 1, #targets),
                #targets), nil, SMALL))
        add(details, IRGui.makeButton(m.x, m.y, m.w, m.h, "MORE", function()
            details.page = (details.page + 1) % pages
            M.show(app, details, creation)
        end, SMALL))
    end
end

function M.show(app, details, creation)
    M.clear(details)
    if not creation then
        add(details, IRGui.makeLabel(L.details.name.x, L.details.name.y, "SELECT A CREATION"))
        return
    end
    add(details, IRGui.makeLabel(L.details.name.x, L.details.name.y,
        catalogView.displayName(creation)))
    local f = L.details.fav
    add(details, IRGui.makeButton(f.x, f.y, f.w, f.h,
        app.settings.favourites[creation.dir] and "UNFAV" or "FAV",
        function() app.toggleFavourite(creation.dir) end, SMALL))

    local manifest = creation.manifest
    local cols = L.details.cols
    local description
    if manifest then
        description = manifest.description
    elseif creation.error then
        local e = creation.error
        description = string.format("INVALID MANIFEST: %s %s (LINE %d COL %d)",
            e.path or "", e.message or "", e.line or 0, e.column or 0)
    else
        description = "NO CREATION.JSON IN THIS DIRECTORY"
    end
    local lines = wrap(description, cols, L.details.descriptionLines)
    for index, line in ipairs(lines) do
        add(details, IRGui.makeLabel(L.details.left,
            L.details.descriptionTop + (index - 1) * L.details.lineStep, line, nil, SMALL))
    end
    if manifest and #manifest.tags > 0 then
        add(details, IRGui.makeLabel(L.details.left, L.details.tagsY,
            clip("TAGS: " .. table.concat(manifest.tags, ", "), cols), nil, SMALL))
    end
    add(details, IRGui.makeLabel(L.details.left, L.details.dirY, clip(creation.dir, cols), nil,
        SMALL))
    local ide, folder = L.details.ide, L.details.folder
    add(details, IRGui.makeButton(ide.x, ide.y, ide.w, ide.h, "IDE",
        function() app.openInEditor(creation.dir) end, SMALL))
    add(details, IRGui.makeButton(folder.x, folder.y, folder.w, folder.h, "FOLDER",
        function() app.openFolder(creation.dir) end, SMALL))

    if manifest then
        for index = 1, math.min(#manifest.docs, L.details.docs) do
            local doc = manifest.docs[index]
            local y = L.details.docsTop + (index - 1) * L.details.docStep
            add(details, IRGui.makeButton(L.details.left, y, 280, 16,
                clip("DOC " .. doc, 44), function() app.openInEditor(doc) end, SMALL))
        end
        showTargets(app, details, creation)
    end
end

-- Records a preset dropdown change so the next show and Enter use it.
function M.pollPresets(app, details)
    for _, entry in ipairs(details.presets) do
        local selected = IRGui.dropdownSelected(entry.dropdown)
        if selected ~= entry.selected and selected > 0 then
            entry.selected = selected
            app.setPresetIndex(entry.target, selected)
        end
    end
end

function M.selectedPreset(details, targetName)
    for _, entry in ipairs(details.presets) do
        if entry.target == targetName then
            return entry.selected
        end
    end
    return nil
end

return M
