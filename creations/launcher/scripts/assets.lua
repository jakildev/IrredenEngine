local L = require("launcher_layout")
local WidgetList = require("widget_list")

local M = {}
local SMALL = IRText.Size.SMALL
local KINDS = {
    { key = "voxel_set", label = "VOX" },
    { key = "prefab", label = "PFB" },
    { key = "png", label = "PNG" },
    { key = "data", label = "DATA" },
}

local function basename(path)
    return path:match("([^/]+)$") or path
end

local function dirname(path)
    return path:match("^(.*)/[^/]+$") or "."
end

local function boundsOversize(bounds)
    if not bounds then
        return false
    end
    local minX, minY, minZ, maxX, maxY, maxZ = bounds:match(
        "min=([%-0-9]+),([%-0-9]+),([%-0-9]+);max=([%-0-9]+),([%-0-9]+),([%-0-9]+)"
    )
    if not minX then
        return false
    end
    return tonumber(maxX) - tonumber(minX) > 64 or
        tonumber(maxY) - tonumber(minY) > 64 or
        tonumber(maxZ) - tonumber(minZ) > 64
end

local function add(browser, id)
    browser.widgets[#browser.widgets + 1] = id
    return id
end

local function addPreview(browser, id)
    browser.preview.widgets[#browser.preview.widgets + 1] = id
    return id
end

local function clearWidgets(browser)
    WidgetList.clear(browser.widgets)
end

local function clearPreview(browser)
    local slot = browser.preview
    for _, id in ipairs(slot.widgets or {}) do
        IRGui.destroyWidget(id)
    end
    if slot.image then
        IRGui.destroyWidget(slot.image)
    end
    if slot.drag then
        IRGui.destroyWidget(slot.drag)
    end
    if slot.viewport then
        IRRender.destroyViewport(slot.viewport)
        browser.releasedViewport = slot.viewport
    end
    if slot.entity then
        IREntity.destroyTree(slot.entity)
    end
    browser.preview = { zoom = 8, yaw = 0, widgets = {} }
end

local function report(browser, kind, path)
    if not IRTest then
        return
    end
    local slot = browser.preview
    IRTest.report("assetKind", kind or "")
    IRTest.report("assetPath", path or "")
    IRTest.report("assetEntity", slot.entity and tostring(slot.entity.entity or slot.entity) or "")
    IRTest.report("assetViewport", slot.viewport and tostring(slot.viewport) or "")
    IRTest.report("assetImage", slot.image and tostring(slot.image) or "")
    IRTest.report("assetHasEntity", slot.entity and "true" or "false")
    IRTest.report("assetHasViewport", slot.viewport and "true" or "false")
    IRTest.report("entityCount", tostring(IRTest.entityCount()))
end

local function addMetadata(browser, asset)
    local fieldNames = {}
    for name, _ in pairs(asset.fields) do
        fieldNames[#fieldNames + 1] = name
    end
    table.sort(fieldNames)
    local metadata = {}
    for _, name in ipairs(fieldNames) do
        metadata[#metadata + 1] = name .. ": " .. tostring(asset.fields[name])
    end
    if #asset.parts > 0 then
        metadata[#metadata + 1] = "parts: " .. table.concat(asset.parts, ", ")
    end
    for index = 1, math.min(#metadata, L.assets.metadataRows) do
        addPreview(browser, IRGui.makeLabel(
            L.assets.left, L.assets.metadataTop + (index - 1) * L.assets.metadataStep,
            metadata[index], nil, SMALL))
    end
end

local function placeViewport(browser)
    local p = L.assets.preview
    local slot = browser.preview
    IRRender.setViewportRect(slot.viewport, p.x, p.y, p.w, p.h)
    slot.drag = IRGui.makeHitRegion(p.x, p.y, p.w, p.h, {})
    slot.placed = { x = p.x, y = p.y, w = p.w, h = p.h }
    for index, zoom in ipairs({ 4, 8, 16 }) do
        addPreview(browser, IRGui.makeButton(p.x + (index - 1) * 40, p.y + p.h + 3, 36, 14,
            tostring(zoom) .. "X", function()
                slot.zoom = zoom
                IRRender.setViewportCamera(slot.viewport, slot.zoom, slot.yaw)
            end, SMALL))
    end
end

local function makeViewport(browser, entity)
    local p = L.assets.preview
    local slot = browser.preview
    slot.entity = entity
    slot.viewport = IRRender.createViewport({
        x = p.x, y = p.y, width = p.w, height = p.h, zoom = slot.zoom, yaw = slot.yaw,
    })
    IRRender.setViewportSubject(slot.viewport, entity)
    placeViewport(browser)
end

local function preview(browser, asset)
    clearPreview(browser)
    local p = L.assets.preview
    addMetadata(browser, asset)
    if asset.kind == "png" then
        browser.preview.image = IRGui.makeImage(p.x, p.y, p.w, p.h, asset.path)
        if IRTest then
            local released = browser.releasedViewport
            local leaked = released and pcall(IRRender.getViewportSubject, released)
            IRTest.report("assetPngOnly", leaked and "false" or "true")
        end
    elseif asset.kind == "voxel_set" then
        if boundsOversize(asset.fields.bounds) then
            browser.preview.oversize = addPreview(browser,
                IRGui.makeLabel(p.x, p.y, "TOO LARGE TO PREVIEW", nil, SMALL))
            if IRTest then
                IRTest.report("assetOversize", "true")
            end
        else
            local ok, value = pcall(IRAsset.loadVoxelSet, IRLauncher.assetPath(asset.path),
                IRComponent.EntityAnchor.CENTER)
            if ok then
                makeViewport(browser,
                    IREntity.createPreviewEntity(C_LocalTransform.new(vec3.new(0, 0, 0)), value))
            else
                addPreview(browser, IRGui.makeLabel(p.x, p.y, "LOAD FAILED", nil, SMALL))
                browser.app.log("preview failed: " .. tostring(value))
            end
        end
    elseif asset.kind == "prefab" then
        local ok, entity, err = pcall(function()
            Prefab.register("__launcher_preview", IRLauncher.assetPath(asset.path))
            return Prefab.spawn("__launcher_preview", nil)
        end)
        if ok and entity then
            makeViewport(browser, entity)
        else
            addPreview(browser, IRGui.makeLabel(p.x, p.y, "LOAD FAILED", nil, SMALL))
            browser.app.log("preview failed: " .. tostring(ok and err or entity))
        end
    else
        addPreview(browser, IRGui.makeLabel(p.x, p.y, "NO PREVIEW", nil, SMALL))
    end
    browser.selected = asset
    browser.sequence[#browser.sequence + 1] = asset.kind
    browser.selectionFrames = 0
    report(browser, asset.kind, asset.path)
end

local function assetsOfKind(browser)
    local result = {}
    local kind = KINDS[browser.kind].key
    for _, asset in ipairs(browser.index.entries) do
        if asset.kind == kind then
            result[#result + 1] = asset
        end
    end
    return result
end

function M.rebuild(browser, keepPreview)
    clearWidgets(browser)
    if not keepPreview then
        clearPreview(browser)
    end
    local assets = assetsOfKind(browser)
    browser.scroll = math.min(browser.scroll, math.max(0, #assets - L.assets.rows))
    for slot = 1, L.assets.rows do
        local asset = assets[browser.scroll + slot]
        if not asset then
            break
        end
        local y = L.assets.rowTop + (slot - 1) * L.assets.rowStep
        add(browser, IRGui.makeButton(L.assets.left, y, L.assets.listWidth, 14,
            basename(asset.path), function() preview(browser, asset) end, SMALL))
    end
    if #assets == 0 then
        add(browser, IRGui.makeLabel(L.assets.left, L.assets.rowTop, "NO ASSETS", nil, SMALL))
    end
    local shown = math.min(#assets, L.assets.rows)
    IRGui.setLabelText(browser.countLabel, string.format("%d-%d/%d",
        #assets == 0 and 0 or browser.scroll + 1, browser.scroll + shown, #assets))
    return assets
end

function M.new(app)
    local browser = {
        app = app,
        index = IRCatalog.assets({ root = IRLauncher.catalogRoot() }),
        kind = 1,
        scroll = 0,
        widgets = {},
        chrome = {},
        preview = { zoom = 8, yaw = 0, widgets = {} },
        sequence = {},
    }
    browser.panel = IRGui.makePanel(L.assets.x, L.assets.y, L.assets.w, L.assets.h, "ASSETS")
    M.reflow(browser)
    return browser
end

local function buildChrome(browser)
    for index, kind in ipairs(KINDS) do
        local x = L.assets.left + (index - 1) * L.assets.tabStep
        browser.chrome[#browser.chrome + 1] =
            IRGui.makeButton(x, L.assets.tabY, L.assets.tabWidth, 14, kind.label, function()
            browser.kind = index
            browser.scroll = 0
            M.rebuild(browser)
        end, SMALL)
    end
    browser.chrome[#browser.chrome + 1] = IRGui.makeButton(
        L.assets.up.x, L.assets.up.y, L.assets.up.w, L.assets.up.h, "UP", function()
        browser.scroll = math.max(0, browser.scroll - L.assets.rows + 1)
        M.rebuild(browser, true)
    end, SMALL)
    browser.chrome[#browser.chrome + 1] = IRGui.makeButton(
        L.assets.down.x, L.assets.down.y, L.assets.down.w, L.assets.down.h,
        "DN", function()
            local assets = assetsOfKind(browser)
            browser.scroll = math.min(browser.scroll + L.assets.rows - 1,
                math.max(0, #assets - L.assets.rows))
            M.rebuild(browser, true)
        end, SMALL)
    browser.countLabel = IRGui.makeLabel(L.assets.count.x, L.assets.count.y, "", nil, SMALL)
    browser.chrome[#browser.chrome + 1] = browser.countLabel
    browser.chrome[#browser.chrome + 1] = IRGui.makeButton(
        L.assets.ide.x, L.assets.ide.y, L.assets.ide.w, L.assets.ide.h,
        "IDE", function() M.openSelected(browser) end, SMALL)
end

function M.reflow(browser)
    local selected = browser.selected
    local slot = browser.preview
    local liveViewport = selected and slot.viewport
    local oversize = slot.oversize ~= nil
    WidgetList.clear(browser.chrome)
    clearWidgets(browser)
    buildChrome(browser)
    M.rebuild(browser, liveViewport)
    if liveViewport then
        WidgetList.clear(slot.widgets)
        slot.widgets = {}
        if slot.drag then
            IRGui.destroyWidget(slot.drag)
            slot.drag = nil
        end
        slot.oversize = nil
        addMetadata(browser, selected)
        placeViewport(browser)
        if oversize then
            slot.oversize = addPreview(browser,
                IRGui.makeLabel(L.assets.preview.x, L.assets.preview.y,
                    "TOO LARGE TO PREVIEW", nil, SMALL))
        end
    elseif selected then
        preview(browser, selected)
    else
        clearPreview(browser)
    end
end

function M.poll(browser)
    local slot = browser.preview
    browser.selectionFrames = (browser.selectionFrames or 0) + 1
    if slot.drag and IRGui.isPressed(slot.drag) then
        local mouseX = IRInput.mouseIsoScreen()
        if slot.lastMouseX then
            slot.yaw = slot.yaw + (mouseX - slot.lastMouseX) * 0.01
            IRRender.setViewportCamera(slot.viewport, slot.zoom, slot.yaw)
        end
        slot.lastMouseX = mouseX
    elseif slot.drag then
        slot.lastMouseX = nil
    end
    if slot.viewport and not slot.oversize and IRRender.viewportSubjectOversize(slot.viewport) then
        slot.oversize = addPreview(browser,
            IRGui.makeLabel(L.assets.preview.x, L.assets.preview.y, "TOO LARGE TO PREVIEW", nil, SMALL))
        IRRender.setViewportVisible(slot.viewport, false)
        if IRTest then
            IRTest.report("assetOversize", "true")
        end
    end
    if IRTest then
        if browser.reportedSequence ~= #browser.sequence then
            IRTest.report("assetSequence", table.concat(browser.sequence, "|"))
            browser.reportedSequence = #browser.sequence
        end
        if slot.entity and browser.selectionFrames >= 2 then
            local count = IRTest.entityCount()
            local drawn = slot.viewport and IRRender.getViewportSubject(slot.viewport)
            IRTest.report("assetSubjectMatches",
                drawn and tostring(drawn) == tostring(slot.entity.entity or slot.entity)
                    and "true" or "false")
            if not browser.firstVoxelCount and #browser.sequence == 1 then
                browser.firstVoxelCount = count
            elseif browser.firstVoxelCount and #browser.sequence >= 3 and
                browser.sequence[#browser.sequence] == "voxel_set" and
                browser.sequence[#browser.sequence - 1] == "png" then
                IRTest.report("assetCountRestored",
                    count == browser.firstVoxelCount and "true" or "false")
            end
        end
    end
end

function M.previewIndex(browser, kindKey, index)
    for kindIndex, kind in ipairs(KINDS) do
        if kind.key == kindKey then
            browser.kind = kindIndex
            local assets = M.rebuild(browser)
            if assets[index] then
                preview(browser, assets[index])
                return true
            end
            return false
        end
    end
    return false
end

function M.openSelected(browser)
    if browser.selected then
        browser.app.openInEditor(dirname(browser.selected.path))
    end
end

return M
