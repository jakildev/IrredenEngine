-- IRLauncher: browse, build and run the creations of the checkout this
-- launcher was built from and of the worktrees configured beside it. The
-- catalog comes from IRCatalog, Build and Package are the `ir-build` tool run
-- through IREngine.spawnTool and polled into the log from a RENDER-phase
-- system, and Run hands the process off to the target through
-- IREngine.handOff from an INPUT-phase system, after a Build when the target
-- is not built yet. Only one child runs at a time, and every creation action
-- first makes that creation's root the active root, so it runs in that
-- checkout and its build tree.

package.path = "scripts/?.lua;" .. package.path

local L = require("launcher_layout")
local catalogView = require("catalog_view")
local details = require("details")
local logPanel = require("log_panel")
local settingsModule = require("settings")
local assetsModule = require("assets")
local WidgetList = require("widget_list")

local SMALL = IRText.Size.SMALL
local SETTINGS_STORE = "launcher"
local LOG_FILE = "save_files/launcher.log"
local BASE_ROOT = settingsModule.BASE_ROOT
local CATALOG_ROOT = IRLauncher.catalogRoot()

local app = {
    creations = {},
    byDir = {},
    selectedDir = nil,
    selectedTarget = 1,
    tool = nil,
    pendingRun = nil,
    launches = 0,
    -- A `--catalog-root` fixture is a read-only catalog source: its rows run
    -- as the base root and it never joins the root list.
    fixtureCatalog = CATALOG_ROOT ~= ".",
}
local SESSION = tostring(app)

function app.discover()
    if app.fixtureCatalog then
        return { BASE_ROOT }
    end
    local ok, roots = pcall(IRCatalog.roots)
    return ok and roots or { BASE_ROOT }
end

app.discoveredRoots = app.discover()
app.settings = settingsModule.new(SETTINGS_STORE)
-- A scripted capture run starts from empty settings, so no earlier run's
-- favourites, folds or roots move the rows it clicks or the screen it captures.
if IRLauncher.scripted() then
    IRSave.clear(SETTINGS_STORE)
    IRSave.save(SETTINGS_STORE)
end
settingsModule.load(app.settings, app.discoveredRoots)

-- The configured roots in id order, the base first.
function app.rootList()
    if app.fixtureCatalog then
        return { BASE_ROOT }
    end
    return settingsModule.rootList(app.settings)
end

function app.assetRoot()
    return app.fixtureCatalog and CATALOG_ROOT or app.settings.activeRoot
end

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
app.defaultLayout = nil

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
    local ok, result
    if app.fixtureCatalog then
        ok, result = pcall(IRCatalog.scan, { root = CATALOG_ROOT, buildDir = "build" })
    else
        ok, result = pcall(IRCatalog.scanRoots, { roots = app.rootList() })
    end
    if not ok then
        app.log("catalog scan failed: " .. tostring(result))
        return
    end
    app.creations, app.byDir = {}, {}
    for index = 1, #result.creations do
        local creation = result.creations[index]
        if app.fixtureCatalog then
            creation.root = BASE_ROOT
        end
        app.creations[#app.creations + 1] = creation
        app.byDir[creation.dir] = creation
    end
    settingsModule.migrateLegacyPresets(app.settings, app.creations)
    if IRTest then
        local lastRuns = {}
        for _, creation in ipairs(app.creations) do
            for _, target in ipairs(creation.targets) do
                lastRuns[#lastRuns + 1] = creation.root .. ":" .. target.name .. "=" ..
                    (target.lastRun and target.lastRun.exe or "-")
            end
        end
        IRTest.report("lastRuns", table.concat(lastRuns, "|"))
    end
    if app.selectedDir and not app.byDir[app.selectedDir] then
        app.selectedDir = nil
    end
    catalogView.rebuild(app, app.tree)
    app.refreshDetails()
end

-- Header worktree picker: the configured roots, present only while there are
-- two or more of them.
local picker = { widgets = {}, roots = {}, selected = 0 }

function app.refreshPicker()
    WidgetList.clear(picker.widgets)
    picker.id = nil
    picker.roots = app.rootList()
    if #picker.roots >= 2 then
        picker.selected = 1
        for index, root in ipairs(picker.roots) do
            if root == app.settings.activeRoot then
                picker.selected = index
            end
        end
        local hp = IRLauncher.updateFailedLog() and L.header.pickerBesideBanner or L.header.picker
        picker.id = IRGui.makeDropdown(hp.x, hp.y, hp.w, hp.h, picker.roots, picker.selected, hp.h,
            SMALL)
        picker.widgets[1] = picker.id
    end
    if IRTest then
        IRTest.report("picker", picker.id and table.concat(picker.roots, "|") or "none")
    end
end

function app.reportRoots()
    settingsModule.report(app.settings)
    if IRTest then
        IRTest.report("discoveredRoots", table.concat(app.discoveredRoots, "|"))
    end
end

-- Makes @p root the active root before process work in it. IRLauncher.selectRoot
-- re-checks the id against fresh discovery and refuses while a tool runs, so
-- a root that vanished, or a busy slot, launches nothing.
function app.confirmRoot(root, what)
    local refusal = IRLauncher.selectRoot(root)
    if refusal then
        app.log(what .. ": root " .. root .. " is unavailable: " .. refusal)
        return false
    end
    if app.settings.activeRoot ~= root then
        app.settings.activeRoot = root
        if not app.fixtureCatalog then
            assetsModule.setRoot(app.assetBrowser, app.assetRoot())
        end
        app.refreshPicker()
        app.reportRoots()
    end
    return true
end

-- The picker's switch: no restart, no rescan; the selection and an open
-- settings panel close, since they may belong to the previous root.
function app.switchRoot(root)
    if app.confirmRoot(root, "ROOT") then
        app.selectedDir = nil
        if app.settings.open then
            settingsModule.close(app.settings)
        end
        catalogView.rebuild(app, app.tree)
        app.refreshDetails()
        app.log("ROOT " .. root .. " active")
        if IRTest then
            IRTest.report("sessionAtSwitch", SESSION)
        end
    else
        app.refreshPicker()
    end
end

function app.toggleRoot(root)
    if root == BASE_ROOT then
        return
    end
    if app.settings.roots[root] then
        if app.settings.activeRoot == root and not app.confirmRoot(BASE_ROOT, "DROP " .. root) then
            return
        end
        app.settings.roots[root] = nil
    else
        app.settings.roots[root] = true
    end
    app.selectedDir = nil
    app.rescan()
    app.refreshPicker()
    settingsModule.show(app, app.settings)
    app.reportRoots()
end

function app.select(dir)
    local creation = app.byDir[dir]
    if creation and not app.confirmRoot(creation.root, "SELECT " .. dir) then
        return
    end
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

function app.presetIndex(presetKey, count)
    local stored = app.settings.presets[presetKey]
    if stored and stored >= 1 and stored <= count then
        return stored
    end
    return 1
end

function app.setPresetIndex(presetKey, index)
    app.settings.presets[presetKey] = index
    settingsModule.report(app.settings)
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

local function countLaunch()
    app.launches = app.launches + 1
    if IRTest then
        IRTest.report("launches", tostring(app.launches))
    end
end

-- @p spawnTool defaults to IREngine.spawnTool, whose requests run in the
-- active root.
local function spawn(kind, label, request, onExit, spawnTool)
    local ok, handle = pcall(spawnTool or IREngine.spawnTool, request)
    if not ok then
        app.log(label .. " failed to start: " .. tostring(handle))
        return false
    end
    countLaunch()
    app.tool = { kind = kind, label = label, handle = handle, onExit = onExit }
    app.log(label .. " started")
    return true
end

function app.build(creation, targetName, onSuccess)
    if busy("BUILD " .. targetName) or not app.confirmRoot(creation.root, "BUILD " .. targetName) then
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

-- @p creation's target as the latest scan has it.
local function findTargetState(creation, targetName)
    local current = app.byDir[creation.dir]
    for _, target in ipairs(current and current.targets or {}) do
        if target.name == targetName then
            return target
        end
    end
    return nil
end

-- Builds `<target>Package`, the target irreden_package_target adds, in the
-- creation's root. The archive path is what the rescan finds in that root's
-- build tree, never one predicted here.
function app.package(creation, targetName)
    local label = "PACKAGE " .. targetName
    if busy(label) or not app.confirmRoot(creation.root, label) then
        return
    end
    spawn("package", label, { tool = "ir-build", target = targetName .. "Package" },
        function(code)
            app.log(string.format("%s exit %d", label, code))
            if IRTest then
                IRTest.report("packageExit", targetName .. "=" .. code)
            end
            app.rescan()
            if code ~= 0 then
                return
            end
            local target = findTargetState(creation, targetName)
            if target and target.packagePath ~= "" then
                app.log(label .. " archive " .. target.packagePath)
            else
                app.log(label .. ": no archive found in the build directory")
            end
        end)
end

local function currentPresetArgs(creation, index)
    local target = creation.manifest.targets[index]
    local presets = target.presets
    if #presets == 0 then
        return {}
    end
    local presetKey = settingsModule.presetKey(creation, target.name)
    local selected = details.selectedPreset(app.details, presetKey)
        or app.presetIndex(presetKey, #presets)
    return presets[selected].args
end

function app.run(index)
    local creation = app.selected()
    if not creation or not creation.manifest or not creation.targets[index] then
        return
    end
    local target = creation.targets[index]
    if busy("RUN " .. target.name) or app.pendingRun or
        not app.confirmRoot(creation.root, "RUN " .. target.name) then
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
        app.pendingRun = { target = target.name, argv = argv, root = creation.root }
    end
    if target.built then
        queue()
    else
        app.log("RUN " .. target.name .. ": not built yet, building first")
        app.build(creation, target.name, queue)
    end
end

local function handOff(pending)
    if not app.confirmRoot(pending.root, "RUN " .. pending.target) then
        return
    end
    app.log("RUN " .. table.concat(pending.argv, " "))
    countLaunch()
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

-- @p root is the creation root @p path belongs to, or nil for a launcher-owned
-- file, which IRLauncher.spawnBaseTool opens without touching the active root.
local function openTool(tool, root, path, label)
    if busy(label) or (root and not app.confirmRoot(root, label)) then
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
    end, not root and IRLauncher.spawnBaseTool or nil)
end

function app.openInEditor(root, path)
    openTool("editor", root, path, "OPEN " .. path)
end

function app.openFolder(root, path)
    openTool("open-folder", root, path, "FOLDER " .. path)
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
    openTool("editor", nil, runDir .. "/" .. LOG_FILE, "OPEN LOG")
end

function app.openSettings()
    if app.settings.open then
        return
    end
    app.discoveredRoots = app.discover()
    app.reportRoots()
    details.clear(app.details)
    settingsModule.show(app, app.settings)
end

function app.closeSettings()
    settingsModule.close(app.settings)
    app.refreshDetails()
end

function app.revertSettings()
    app.discoveredRoots = app.discover()
    settingsModule.load(app.settings, app.discoveredRoots)
    -- Load already fell back to `.` for a stored root that is gone.
    local active = app.settings.activeRoot
    app.settings.activeRoot = nil
    if not app.confirmRoot(active, "REVERT") then
        app.settings.activeRoot = active
    end
    app.selectedDir = nil
    app.rescan()
    app.refreshPicker()
    local layout = app.settings.layout ~= "" and app.settings.layout or app.defaultLayout
    if layout and not IRLayout.deserialize(layout) then
        app.log("settings layout could not be restored")
    end
    app.tree.collapsed = app.settings.collapsed
    settingsModule.show(app, app.settings)
    app.reportRoots()
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
        openTool("editor", nil, failedLog, "OPEN " .. failedLog)
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
        app.build(creation, creation.targets[app.selectedTarget].name)
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
        app.defaultLayout = app.defaultLayout or IRLayout.serialize()
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
    if picker.id then
        local selected = IRGui.dropdownSelected(picker.id)
        if selected ~= picker.selected and selected > 0 then
            picker.selected = selected
            app.switchRoot(picker.roots[selected])
        end
    end
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

if not app.confirmRoot(app.settings.activeRoot, "ROOT") then
    app.settings.activeRoot = BASE_ROOT
    app.confirmRoot(BASE_ROOT, "ROOT")
    assetsModule.setRoot(app.assetBrowser, app.assetRoot())
end
assetsModule.report(app.assetBrowser)
app.rescan()
app.refreshPicker()
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
if IRLauncher.session() == "multi_root" then
    -- The legacy preset migration over this session's catalog: the exact
    -- base-root target name migrates once, a renamed-away name is dropped.
    local probe = settingsModule.new("launcher_migration_probe")
    probe.legacyPresets = { IRLauncherStub = 2, IRLauncherStubRenamed = 3 }
    settingsModule.migrateLegacyPresets(probe, app.creations)
    local migrated = {}
    for key, value in pairs(probe.presets) do
        migrated[#migrated + 1] = key .. "=" .. value
    end
    table.sort(migrated)
    IRTest.report("legacyMigration", table.concat(migrated, ",") .. "|legacy=" ..
        tostring(next(probe.legacyPresets) == nil and 0 or 1))
end
if IRTest then
    IRTest.report("sessionStart", SESSION)
    IRTest.report("launches", "0")
end
app.log(string.format("catalog: %d creations under %s", #app.creations,
    app.fixtureCatalog and CATALOG_ROOT or table.concat(app.rootList(), ", ")))
app.reportRoots()
