-- IRLauncher: browse, build and run the creations of the checkout this
-- launcher was built from. The catalog comes from IRCatalog, Build is the
-- `ir-build` tool run through IREngine.spawnTool and polled into the log
-- from a RENDER-phase system, and Run hands the process off to the target
-- through IREngine.handOff from an INPUT-phase system, after a Build when the
-- target is not built yet. Only one child runs at a time.

package.path = "scripts/?.lua;" .. package.path

local L = require("launcher_layout")
local catalogView = require("catalog_view")
local details = require("details")
local logPanel = require("log_panel")
local settingsModule = require("settings")
local assetsModule = require("assets")

local SMALL = IRText.Size.SMALL
local SETTINGS_STORE = "launcher"
local LOG_FILE = "save_files/launcher.log"

local app = {
    creations = {},
    byDir = {},
    selectedDir = nil,
    selectedTarget = 1,
    tool = nil,
    pendingRun = nil,
}

app.settings = settingsModule.new(SETTINGS_STORE)
-- A scripted capture run starts from empty settings, so no earlier run's
-- favourites or folds move the rows it clicks or the screen it captures.
if IRLauncher.scripted() then
    IRSave.clear(SETTINGS_STORE)
    IRSave.save(SETTINGS_STORE)
end
settingsModule.load(app.settings)

app.logView = logPanel.new(app)

function app.log(line)
    logPanel.append(app.logView, line)
    if IRTest then
        IRTest.report("log", logPanel.text(app.logView))
    end
end

app.tree = catalogView.new(app)
app.tree.collapsed = app.settings.collapsed
app.details = details.new(app)
app.assetBrowser = assetsModule.new(app)

local fraction = function(value, minPx, maxPx)
    return { mode = IRLayout.SizeMode.FRACTION, value = value, minPx = minPx, maxPx = maxPx }
end
local fixed = function(value, minPx, maxPx)
    return { mode = IRLayout.SizeMode.FIXED_PX, value = value, minPx = minPx, maxPx = maxPx }
end
local root = IRLayout.makeRow(nil, fraction(1), "launcher")
IRLayout.makeLeaf(root, fixed(200, 140, 320), app.tree.panel, "catalog")
local right = IRLayout.makeColumn(root, fraction(1), "right")
local top = IRLayout.makeRow(right, fraction(1, 180), "top")
IRLayout.makeLeaf(top, fraction(1, 180), app.details.panel, "details")
IRLayout.makeLeaf(top, fixed(136, 110, 240), app.assetBrowser.panel, "assets")
IRLayout.makeLeaf(right, fixed(238, 100, 360), app.logView.panel, "log")
IRLayout.buildSplitters()
IRLayout.setRoot(root, 0, 30, 640, 690)
if app.settings.layout ~= "" then
    IRLayout.deserialize(app.settings.layout)
end
app.layoutRevision = IRLayout.revision()
app.layoutWidths = {}

function app.selected()
    return app.selectedDir and app.byDir[app.selectedDir] or nil
end

function app.refreshDetails()
    if app.settings.open then
        return
    end
    details.show(app, app.details, app.selected())
end

function app.rescan()
    local ok, result = pcall(IRCatalog.scan, { root = IRLauncher.catalogRoot(), buildDir = "build" })
    if not ok then
        app.log("catalog scan failed: " .. tostring(result))
        return
    end
    app.creations, app.byDir = {}, {}
    for index = 1, #result.creations do
        local creation = result.creations[index]
        app.creations[#app.creations + 1] = creation
        app.byDir[creation.dir] = creation
    end
    if app.selectedDir and not app.byDir[app.selectedDir] then
        app.selectedDir = nil
    end
    catalogView.rebuild(app, app.tree)
    app.refreshDetails()
end

function app.select(dir)
    if app.selectedDir ~= dir then
        app.selectedDir = dir
        app.selectedTarget = 1
        app.details.page = 0
    end
    catalogView.rebuild(app, app.tree)
    app.refreshDetails()
end

function app.selectTarget(index)
    app.selectedTarget = index
end

function app.presetIndex(targetName, count)
    local stored = app.settings.presets[targetName]
    if stored and stored >= 1 and stored <= count then
        return stored
    end
    return 1
end

function app.setPresetIndex(targetName, index)
    app.settings.presets[targetName] = index
end

function app.toggleFavourite(dir)
    app.settings.favourites[dir] = not app.settings.favourites[dir] or nil
    settingsModule.report(app.settings)
    catalogView.rebuild(app, app.tree)
    app.refreshDetails()
end

-- One child at a time: a live tool run, or a hand-off waiting for one.
local function busy(what)
    if app.tool then
        app.log(what .. ": " .. app.tool.label .. " is still running")
        return true
    end
    return false
end

local function spawn(kind, label, request, onExit)
    local ok, handle = pcall(IREngine.spawnTool, request)
    if not ok then
        app.log(label .. " failed to start: " .. tostring(handle))
        return false
    end
    app.tool = { kind = kind, label = label, handle = handle, onExit = onExit }
    app.log(label .. " started")
    return true
end

function app.build(targetName, onSuccess)
    if busy("BUILD " .. targetName) then
        return
    end
    spawn("build", "BUILD " .. targetName, { tool = "ir-build", target = targetName },
        function(code)
            app.log(string.format("BUILD %s exit %d", targetName, code))
            if IRTest then
                IRTest.report("buildExit", targetName .. "=" .. code)
            end
            app.rescan()
            if code == 0 and onSuccess then
                onSuccess()
            elseif onSuccess then
                app.log("RUN " .. targetName .. " cancelled: the build failed")
            end
        end)
end

local function currentPresetArgs(creation, index)
    local target = creation.manifest.targets[index]
    local presets = target.presets
    if #presets == 0 then
        return {}
    end
    local selected = details.selectedPreset(app.details, target.name)
        or app.presetIndex(target.name, #presets)
    return presets[selected].args
end

function app.run(index)
    local creation = app.selected()
    if not creation or not creation.manifest or not creation.targets[index] then
        return
    end
    local target = creation.targets[index]
    if busy("RUN " .. target.name) or app.pendingRun then
        return
    end
    local argv = { target.name }
    local args = currentPresetArgs(creation, index)
    for argIndex = 1, #args do
        argv[#argv + 1] = args[argIndex]
    end
    argv[#argv + 1] = "--window-mode"
    argv[#argv + 1] = IRLauncher.childWindowMode()
    local function queue()
        app.pendingRun = { target = target.name, argv = argv }
    end
    if target.built then
        queue()
    else
        app.log("RUN " .. target.name .. ": not built yet, building first")
        app.build(target.name, queue)
    end
end

local function handOff(pending)
    app.log("RUN " .. table.concat(pending.argv, " "))
    local ok, result = pcall(IREngine.handOff, { argv = pending.argv })
    local text
    if not ok then
        text = pending.target .. " could not start: " .. tostring(result)
    elseif not result.supported then
        text = pending.target .. " not run: hand-off is unavailable outside ir-run" ..
            " (start the launcher with ir-launcher)"
    else
        text = string.format("%s verdict %s exit %s", pending.target,
            tostring(result.verdict or "-"), tostring(result.exitCode or "-"))
        if result.error and result.error ~= "" then
            text = text .. " (" .. result.error .. ")"
        end
    end
    app.log("RUN " .. text)
    logPanel.setResult(app.logView, text)
    if IRTest then
        IRTest.report("verdict", ok and result.verdict or "")
        IRTest.report("lastRun", text)
    end
    app.rescan()
end

local function openTool(tool, path, label)
    if busy(label) then
        return
    end
    local request = { tool = tool, path = path }
    if tool == "editor" then
        local editor = settingsModule.effectiveEditor(app.settings)
        if not editor then
            app.log(label .. ": neither cursor nor code is on PATH")
            return
        end
        request.editor = editor
    end
    spawn(tool, label, request, function(code)
        if code ~= 0 then
            app.log(string.format("%s exit %d", label, code))
        end
    end)
end

function app.openInEditor(path)
    openTool("editor", path, "OPEN " .. path)
end

function app.openFolder(path)
    openTool("open-folder", path, "FOLDER " .. path)
end

function app.openLogInEditor()
    local runDir = IRLauncher.runDir()
    if not runDir then
        app.log("OPEN LOG: the run directory is outside the tool root")
        return
    end
    if not IRFile.writeText(LOG_FILE, logPanel.text(app.logView)) then
        app.log("OPEN LOG: could not write " .. LOG_FILE)
        return
    end
    app.openInEditor(runDir .. "/" .. LOG_FILE)
end

function app.openSettings()
    if app.settings.open then
        return
    end
    details.clear(app.details)
    settingsModule.show(app, app.settings)
end

function app.closeSettings()
    settingsModule.close(app.settings)
    app.refreshDetails()
end

function app.revertSettings()
    settingsModule.load(app.settings)
    if app.settings.layout ~= "" then
        IRLayout.deserialize(app.settings.layout)
    end
    app.tree.collapsed = app.settings.collapsed
    settingsModule.show(app, app.settings)
    settingsModule.report(app.settings)
    catalogView.rebuild(app, app.tree)
    app.log("settings reverted to the saved copy")
end

-- Header: title, settings, the supervised-only update button, and the banner
-- a failed update relaunches with.
IRGui.makePanel(L.header.x, L.header.y, L.header.w, L.header.h, "", true)
IRGui.makeLabel(8, 9, "IRREDEN LAUNCHER")
local hs = L.header.settings
IRGui.makeButton(hs.x, hs.y, hs.w, hs.h, "SETTINGS", function() app.openSettings() end, SMALL)
if IRLauncher.requestUpdate then
    local hu = L.header.update
    IRGui.makeButton(hu.x, hu.y, hu.w, hu.h, "UPDATE", function()
        settingsModule.save(app.settings)
        IRLauncher.requestUpdate()
    end, SMALL)
end
local failedLog = IRLauncher.updateFailedLog()
if failedLog then
    IRGui.makeLabel(L.header.banner.x, L.header.banner.y, "UPDATE FAILED - PREVIOUS LAUNCHER", nil,
        SMALL)
    local ho = L.header.openLog
    IRGui.makeButton(ho.x, ho.y, ho.w, ho.h, "UPDATE LOG", function()
        app.openInEditor(failedLog)
    end, SMALL)
end

-- Keyboard: / focuses the filter, Enter runs the selected target's preset, B
-- builds it, Escape clears the filter and the selection. A focused text input
-- captures keys, so typing into the filter never fires these.
local KEY, PRESSED = IRInput.InputType.KEY_MOUSE, IRInput.ButtonStatus.PRESSED
IRCommand.createCommand(KEY, PRESSED, IRInput.Key.SLASH, function()
    IRGui.focus(app.tree.filter)
end, nil, nil, "Filter", "Focus the catalog filter")
IRCommand.createCommand(KEY, PRESSED, IRInput.Key.ENTER, function()
    app.run(app.selectedTarget)
end, nil, nil, "Run", "Run the selected target with its current preset")
IRCommand.createCommand(KEY, PRESSED, IRInput.Key.B, function()
    local creation = app.selected()
    if creation and creation.targets[app.selectedTarget] then
        app.build(creation.targets[app.selectedTarget].name)
    end
end, nil, nil, "Build", "Build the selected target")
IRCommand.createCommand(KEY, PRESSED, IRInput.Key.ESCAPE, function()
    app.selectedDir = nil
    catalogView.clearFilter(app, app.tree)
    app.refreshDetails()
end, nil, nil, "Clear", "Clear the filter and the selection")

local C_LauncherTick = IRComponent.register("LauncherTick", { n = 0 })
IREntity.singleton(C_LauncherTick)

-- Polls the live tool into the log, reads the widgets the model follows, and
-- redraws the log window. Both systems defer their work out of the tick,
-- because a rescan or a filter change rebuilds widgets.
local function poll()
    local revision = IRLayout.revision()
    if revision ~= app.layoutRevision then
        app.layoutRevision = revision
        local tx, ty, tw, th = IRLayout.leafRect(app.tree.panel)
        local dx, dy, dw, dh = IRLayout.leafRect(app.details.panel)
        local ax, ay, aw, ah = IRLayout.leafRect(app.assetBrowser.panel)
        local lx, ly, lw, lh = IRLayout.leafRect(app.logView.panel)
        L.update(
            { x = tx, y = ty, w = tw, h = th },
            { x = dx, y = dy, w = dw, h = dh },
            { x = ax, y = ay, w = aw, h = ah },
            { x = lx, y = ly, w = lw, h = lh })
        catalogView.reflow(app, app.tree)
        if app.settings.open then
            settingsModule.show(app, app.settings)
        else
            details.reflow(app, app.details)
        end
        assetsModule.reflow(app.assetBrowser)
        logPanel.reflow(app.logView, app)
        if IRTest then
            local last = app.layoutWidths[#app.layoutWidths]
            if last ~= tw then
                app.layoutWidths[#app.layoutWidths + 1] = tw
            end
            IRTest.report("layoutWidths", table.concat(app.layoutWidths, "|"))
            IRTest.report("layoutPanelXs", string.format("%d|%d", ax, dx))
            -- Each `placed` rect is copied when its zone last built that widget,
            -- so a zone that skipped its reflow reports its old leaf here.
            local function inside(r, x, y, w, h)
                return r ~= nil and r.x >= x and r.y >= y and r.x + r.w <= x + w and
                    r.y + r.h <= y + h
            end
            local detailsPlaced = app.settings.open and app.settings.placed or app.details.placed
            local preview = app.assetBrowser.preview.placed
            local contained = inside(app.tree.placed, tx, ty, tw, th) and
                inside(detailsPlaced, dx, dy, dw, dh) and
                inside(preview, ax, ay, aw, ah) and
                inside(app.logView.placed, lx, ly, lw, lh)
            IRTest.report("layoutContained", contained and "true" or "false")
            IRTest.report("layoutPreviewRect", preview and
                string.format("%d,%d,%d,%d", preview.x, preview.y, preview.w, preview.h) or "none")
        end
    end
    if IRTest and app.settings.layout ~= "" then
        IRTest.report("layoutRestored",
            IRLayout.serialize() == app.settings.layout and "true" or "false")
    end
    local tool = app.tool
    if tool then
        local lines = tool.handle:poll()
        for index = 1, #lines do
            app.log(lines[index])
        end
        local code = tool.handle:exit()
        if code ~= nil then
            app.tool = nil
            tool.onExit(code)
        end
    end
    catalogView.pollFilter(app, app.tree)
    details.pollPresets(app, app.details)
    settingsModule.poll(app.settings)
    assetsModule.poll(app.assetBrowser)
    logPanel.render(app.logView)
end

IRSystem.appendSystem(IRTime.RENDER, IRSystem.registerSystem({
    name = "LauncherPoll",
    components = { C_LauncherTick },
    tick = function()
        IREntity.deferredCall(poll)
    end,
}))

-- Hand-offs start from INPUT, never inside the render pipeline the GPU drain
-- would cut in half.
IRSystem.appendSystem(IRTime.INPUT, IRSystem.registerSystem({
    name = "LauncherHandOff",
    components = { C_LauncherTick },
    tick = function()
        local pending = app.pendingRun
        if pending and not app.tool then
            app.pendingRun = nil
            IREntity.deferredCall(function()
                handOff(pending)
            end)
        end
    end,
}))

app.rescan()
if IRLauncher.session() == "asset_preview" or IRLauncher.session() == "dockspace_roundtrip" then
    assetsModule.previewIndex(app.assetBrowser, "voxel_set", 1)
end
if IRLauncher.session() == "thumbnail_chain" then
    local names = { "thumb_manifest", "thumb_reference", "thumb_screenshot", "thumb_none" }
    local paths = {}
    for _, name in ipairs(names) do
        local dir = "creations/launcher/test/fixture_root/creations/" .. name
        paths[#paths + 1] = details.thumbnailFor(app.byDir[dir], IRLauncher.preset(),
            app.assetBrowser.index)
    end
    local labels = {}
    local probes = {}
    for index, path in ipairs(paths) do
        labels[index] = path:match("([^/]+)$") or path
        local image = IRGui.makeImage(210 + (index - 1) * 70, 300, 64, 48, path)
        probes[index] = IRTest.imageProbe(image)
    end
    IRTest.report("thumbnailChain", table.concat(labels, "|"))
    IRTest.report("thumbnailProbes", table.concat(probes, "|"))
end
app.log(string.format("catalog: %d creations under %s", #app.creations, IRLauncher.catalogRoot()))
settingsModule.report(app.settings)
