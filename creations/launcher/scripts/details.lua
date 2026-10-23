-- Center zone: the selected creation. Name, description, tags, docs links,
-- Open in IDE / Open folder, and one row per target with its built state,
-- last verdict, preset dropdown, Build and Run, plus Package and the latest
-- archive for a target whose manifest sets `package`. Every widget here is
-- rebuilt on selection, so the zone holds nothing between selections but its
-- panel.

local L = require("launcher_layout")
local WidgetList = require("widget_list")
local catalogView = require("catalog_view")
local settingsModule = require("settings")

local M = {}

local SMALL = IRText.Size.SMALL
-- Absent on purpose: IRGui.makeImage draws its placeholder for a missing in-base file.
local PLACEHOLDER = "creations/launcher/missing-thumbnail.png"

local function thumbnailIndex(assetIndex)
    if assetIndex.thumbnailIndex then
        return assetIndex.thumbnailIndex
    end
    local index = { references = {}, screenshots = {} }
    for _, asset in ipairs(assetIndex.entries) do
        if asset.kind == "png" then
            local dir, preset = asset.path:match("^(creations/.+)/test/references/([^/]+)/")
            if dir then
                local key = dir .. "|" .. preset
                if not index.references[key] or asset.path < index.references[key] then
                    index.references[key] = asset.path
                end
            else
                dir = asset.path:match("^(creations/.+)/save_files/screenshots/")
                local current = dir and index.screenshots[dir]
                if dir and (not current or asset.mtime > current.mtime) then
                    index.screenshots[dir] = asset
                end
            end
        end
    end
    assetIndex.thumbnailIndex = index
    return index
end

function M.thumbnailFor(creation, preset, assetIndex)
    if not creation or not creation.manifest then
        return PLACEHOLDER
    end
    if creation.manifest.thumbnail and creation.manifest.thumbnail ~= "" then
        return creation.manifest.thumbnail
    end
    local index = thumbnailIndex(assetIndex)
    local reference = index.references[creation.dir .. "|" .. preset]
    local newestScreenshot = index.screenshots[creation.dir]
    return reference or (newestScreenshot and newestScreenshot.path) or PLACEHOLDER
end

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

-- The newest archive on disk, which may predate the last Package run: the
-- run's own result is its exit line in the log.
local function packageText(target, cols)
    if target.packagePath == "" then
        return "NO PACKAGE YET"
    end
    local prefix = "LATEST "
    return prefix .. clip(target.packagePath, cols - #prefix)
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
    local details = { widgets = {}, presets = {}, packages = {}, page = 0 }
    details.panel = IRGui.makePanel(L.details.x, L.details.y, L.details.w, L.details.h, "DETAILS")
    return details
end

function M.reflow(app, details)
    M.show(app, details, app.selected())
end

local function add(details, id)
    details.widgets[#details.widgets + 1] = id
    return id
end

function M.clear(details)
    WidgetList.clear(details.widgets)
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
        local presetKey = settingsModule.presetKey(creation, target.name)
        local selected = app.presetIndex(presetKey, #names)
        local p = L.details.preset
        if #names > 0 then
            local dropdown = add(details,
                IRGui.makeDropdown(p.x, y, p.w, p.h, names, selected, p.h, SMALL))
            details.presets[#details.presets + 1] = {
                dropdown = dropdown, key = presetKey, selected = selected,
            }
        end
        local b, r = L.details.build, L.details.run
        add(details, IRGui.makeButton(b.x, y + b.dy, b.w, b.h, "BUILD", function()
            app.selectTarget(index)
            app.build(creation, target.name)
        end, SMALL))
        add(details, IRGui.makeButton(r.x, y + r.dy, r.w, r.h, "RUN", function()
            app.selectTarget(index)
            app.run(index)
        end, SMALL))
        if manifestTargets[index].package then
            local k = L.details.package
            add(details, IRGui.makeButton(k.x, y + k.dy, k.w, k.h, "PACKAGE", function()
                app.selectTarget(index)
                app.package(creation, target.name)
            end, SMALL))
            add(details, IRGui.makeLabel(L.details.left, y + L.details.packageDy,
                packageText(target, L.details.cols), nil, SMALL))
            details.packages[#details.packages + 1] = target.name .. "=" .. target.packagePath
        end
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

local function reportPackages(details)
    if IRTest then
        IRTest.report("packageControls", tostring(#details.packages))
        IRTest.report("packagePaths", table.concat(details.packages, "|"))
    end
end

function M.show(app, details, creation)
    M.clear(details)
    details.packages = {}
    details.placed = { x = L.details.name.x, y = L.details.name.y, w = 0, h = 0 }
    if not creation then
        add(details, IRGui.makeLabel(L.details.name.x, L.details.name.y, "SELECT A CREATION"))
        reportPackages(details)
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
        local thumbnail = L.details.thumbnail
        local thumbnailPath = M.thumbnailFor(creation, IRLauncher.preset(), app.assetBrowser.index)
        add(details, IRGui.makeImage(thumbnail.x, thumbnail.y, thumbnail.w, thumbnail.h,
            thumbnailPath))
        if IRTest then
            IRTest.report("thumbnail", thumbnailPath)
        end
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
    local dirText = clip(creation.dir, cols)
    if #app.rootList() > 1 then
        -- Stops short of the thumbnail beside it.
        local width = math.floor((L.details.thumbnail.x - L.details.left) / 6)
        local rootDir = creation.root == settingsModule.BASE_ROOT and creation.dir
            or creation.dir:sub(#creation.root + 2)
        dirText = "ROOT " .. clip(creation.root, math.max(4, width - 5))
        if width - #dirText > 12 then
            dirText = dirText .. " " .. clip(rootDir, width - #dirText - 1)
        end
    end
    add(details, IRGui.makeLabel(L.details.left, L.details.dirY, dirText, nil, SMALL))
    if IRTest then
        IRTest.report("detailsRoot", creation.root)
    end
    local ide, folder = L.details.ide, L.details.folder
    add(details, IRGui.makeButton(ide.x, ide.y, ide.w, ide.h, "IDE",
        function() app.openInEditor(creation.root, creation.dir) end, SMALL))
    add(details, IRGui.makeButton(folder.x, folder.y, folder.w, folder.h, "FOLDER",
        function() app.openFolder(creation.root, creation.dir) end, SMALL))

    if manifest then
        for index = 1, math.min(#manifest.docs, L.details.docs) do
            local doc = manifest.docs[index]
            local y = L.details.docsTop + (index - 1) * L.details.docStep
            add(details, IRGui.makeButton(L.details.left, y, 280, 16,
                clip("DOC " .. doc, 44), function() app.openInEditor(creation.root, doc) end,
                SMALL))
        end
        if IRTest then
            IRTest.report("detailsDocs", table.concat(manifest.docs, "|"))
        end
        showTargets(app, details, creation)
    end
    reportPackages(details)
end

-- Records a preset dropdown change so the next show and Enter use it.
function M.pollPresets(app, details)
    for _, entry in ipairs(details.presets) do
        local selected = IRGui.dropdownSelected(entry.dropdown)
        if selected ~= entry.selected and selected > 0 then
            entry.selected = selected
            app.setPresetIndex(entry.key, selected)
        end
    end
end

function M.selectedPreset(details, presetKey)
    for _, entry in ipairs(details.presets) do
        if entry.key == presetKey then
            return entry.selected
        end
    end
    return nil
end

return M
