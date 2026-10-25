-- Launcher settings: the editor entry, favourites, collapsed groups, the
-- last preset per creation target, and the configured and active worktree
-- roots, persisted through IRSave in the run directory's
-- save_files/<store>.irkv. The editor is an entry name from the engine's
-- fixed table, never a command; a stored name that is not found on PATH
-- falls back to the first entry that is. Stored roots are only a wish list:
-- load keeps the ones fresh discovery still reports, and the base `.` is
-- always configured.

local L = require("launcher_layout")
local WidgetList = require("widget_list")

local M = {}

local SMALL = IRText.Size.SMALL
local BASE_ROOT = "."
M.BASE_ROOT = BASE_ROOT

-- Tree fold keys: `<root>\2<group path>`, `<root>\2\1unsorted`, `\3<root>` for
-- a root group, and the global `\1favourites`. A key stored before roots
-- existed belongs to the base root.
local function qualifiedCollapseKey(key)
    if key:find("\2", 1, true) or key:find("^\3") or key == "\1favourites" then
        return key
    end
    return BASE_ROOT .. "\2" .. key
end

M.qualifiedCollapseKey = qualifiedCollapseKey

function M.presetKey(creation, targetName)
    return creation.dir .. "|" .. targetName
end

local function sortedKeys(set)
    local keys = {}
    for key in pairs(set) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    return keys
end

local function listToSet(list)
    local set = {}
    if type(list) == "table" then
        for index = 1, #list do
            set[list[index]] = true
        end
    end
    return set
end

function M.new(store)
    return {
        store = store,
        editor = "",
        favourites = {},
        collapsed = {},
        presets = {},
        legacyPresets = {},
        roots = { [BASE_ROOT] = true },
        activeRoot = BASE_ROOT,
        layout = "",
        widgets = {},
        open = false,
    }
end

-- @p discovered is the fresh IRCatalog.roots() list the stored roots are
-- intersected with.
function M.load(settings, discovered)
    local store = settings.store
    IRSave.load(store)
    settings.editor = IRSave.get(store, "editor", "")
    settings.favourites = listToSet(IRSave.get(store, "favourites"))
    settings.collapsed = {}
    for key in pairs(listToSet(IRSave.get(store, "collapsed"))) do
        settings.collapsed[qualifiedCollapseKey(key)] = true
    end
    settings.presets = {}
    settings.legacyPresets = {}
    settings.layout = IRSave.get(store, "layout", "")
    local presets = IRSave.get(store, "presets")
    if type(presets) == "table" then
        for index = 1, #presets do
            local key, preset = presets[index]:match("^(.+)=(%d+)$")
            if key and key:find("|", 1, true) then
                settings.presets[key] = tonumber(preset)
            elseif key then
                settings.legacyPresets[key] = tonumber(preset)
            end
        end
    end
    local live = listToSet(discovered)
    settings.roots = { [BASE_ROOT] = true }
    for root in pairs(listToSet(IRSave.get(store, "roots"))) do
        if live[root] then
            settings.roots[root] = true
        end
    end
    local active = IRSave.get(store, "active_root", BASE_ROOT)
    settings.activeRoot = settings.roots[active] and active or BASE_ROOT
end

-- A preset stored under a bare target name, before presets were keyed by
-- creation, applies once to the base-root creation that has a target of
-- exactly that name. A name no base-root target carries is dropped, never
-- guessed from a display name.
function M.migrateLegacyPresets(settings, creations)
    for _, creation in ipairs(creations) do
        if creation.root == BASE_ROOT and creation.manifest then
            for _, target in ipairs(creation.manifest.targets) do
                local legacy = settings.legacyPresets[target.name]
                local key = M.presetKey(creation, target.name)
                if legacy and settings.presets[key] == nil then
                    settings.presets[key] = legacy
                end
            end
        end
    end
    settings.legacyPresets = {}
end

function M.rootList(settings)
    return sortedKeys(settings.roots)
end

function M.save(settings)
    local store = settings.store
    IRSave.set(store, "editor", settings.editor)
    IRSave.set(store, "favourites", sortedKeys(settings.favourites))
    IRSave.set(store, "collapsed", sortedKeys(settings.collapsed))
    local presets = {}
    for _, target in ipairs(sortedKeys(settings.presets)) do
        presets[#presets + 1] = target .. "=" .. settings.presets[target]
    end
    IRSave.set(store, "presets", presets)
    IRSave.set(store, "roots", sortedKeys(settings.roots))
    IRSave.set(store, "active_root", settings.activeRoot)
    settings.layout = IRLayout.serialize()
    IRSave.set(store, "layout", settings.layout)
    return IRSave.save(store)
end

-- The index in `editors` (IREngine.toolEditors() rows) of the entry the
-- editor tool runs for the stored name `stored`: that entry when it is found
-- on PATH, else the first found entry, else nil.
function M.editorChoice(editors, stored)
    local first
    for index, entry in ipairs(editors) do
        if entry.found then
            if entry.name == stored then
                return index
            end
            first = first or index
        end
    end
    return first
end

function M.effectiveEditor(settings)
    local editors = IREngine.toolEditors()
    local index = M.editorChoice(editors, settings.editor)
    return index and editors[index].name
end

function M.report(settings)
    if IRTest then
        IRTest.report("settings", settings.editor .. "|" ..
            table.concat(sortedKeys(settings.favourites), ","))
        IRTest.report("configuredRoots", table.concat(sortedKeys(settings.roots), "|"))
        IRTest.report("activeRoot", settings.activeRoot)
        local presets = {}
        for _, key in ipairs(sortedKeys(settings.presets)) do
            presets[#presets + 1] = key .. "=" .. settings.presets[key]
        end
        IRTest.report("presets", table.concat(presets, ","))
        local collapsed = {}
        for _, key in ipairs(sortedKeys(settings.collapsed)) do
            collapsed[#collapsed + 1] = key:gsub("\2", ">")
        end
        IRTest.report("collapsed", table.concat(collapsed, ","))
    end
end

local function add(settings, id)
    settings.widgets[#settings.widgets + 1] = id
    return id
end

function M.close(settings)
    WidgetList.clear(settings.widgets)
    settings.dropdown = nil
    settings.open = false
end

-- One ADD / DROP row per discovered worktree; the base is always configured
-- and has no row. Absent when the catalog is a `--catalog-root` fixture or no
-- worktree is discovered.
local function showWorktrees(app, settings)
    local worktrees = {}
    for _, root in ipairs(app.discoveredRoots) do
        if root ~= BASE_ROOT then
            worktrees[#worktrees + 1] = root
        end
    end
    if #worktrees == 0 then
        return
    end
    local r = L.settings.roots
    local pages = math.ceil(#worktrees / r.rows)
    settings.rootsPage = (settings.rootsPage or 0) % pages
    local first = settings.rootsPage * r.rows
    add(settings, IRGui.makeLabel(r.title.x, r.title.y, string.format("WORKTREES %d-%d OF %d",
        first + 1, math.min(first + r.rows, #worktrees), #worktrees), nil, SMALL))
    if pages > 1 then
        add(settings, IRGui.makeButton(r.more.x, r.more.y, r.more.w, r.more.h, "MORE", function()
            settings.rootsPage = settings.rootsPage + 1
            M.show(app, settings)
        end, SMALL))
    end
    for slot = 1, math.min(#worktrees - first, r.rows) do
        local root = worktrees[first + slot]
        local y = r.top + (slot - 1) * r.step
        add(settings, IRGui.makeButton(r.x, y, r.buttonW, r.buttonH,
            settings.roots[root] and "DROP" or "ADD", function()
                app.toggleRoot(root)
            end, SMALL))
        add(settings, IRGui.makeLabel(r.x + r.labelDx, y + 4, root, nil, SMALL))
    end
end

function M.show(app, settings)
    M.close(settings)
    settings.open = true
    add(settings, IRGui.makeLabel(L.settings.title.x, L.settings.title.y, "SETTINGS"))
    add(settings, IRGui.makeLabel(L.settings.editorLabel.x, L.settings.editorLabel.y,
        "EDITOR FOR OPEN IN IDE", nil, SMALL))
    settings.editors = IREngine.toolEditors()
    local items = {}
    for index, entry in ipairs(settings.editors) do
        items[index] = entry.name .. (entry.found and "" or " (NOT FOUND)")
    end
    -- 0 (nothing selected) when no entry is on PATH: the stored name stays.
    local selected = M.editorChoice(settings.editors, settings.editor) or 0
    local e = L.settings.editor
    settings.dropdown = add(settings,
        IRGui.makeDropdown(e.x, e.y, e.w, e.h, items, selected, e.h, SMALL))
    settings.placed = { x = e.x, y = e.y, w = e.w, h = e.h }
    settings.selected = selected
    if selected > 0 then
        settings.editor = settings.editors[selected].name
    end
    settings.favouritesLabel = add(settings, IRGui.makeLabel(L.settings.favourites.x,
        L.settings.favourites.y, "", nil, SMALL))
    M.refreshFavourites(settings)
    showWorktrees(app, settings)
    local s, r, c = L.settings.save, L.settings.revert, L.settings.close
    add(settings, IRGui.makeButton(s.x, s.y, s.w, s.h, "SAVE", function()
        app.log(M.save(settings) and "settings saved" or "settings could not be saved")
    end, SMALL))
    add(settings, IRGui.makeButton(r.x, r.y, r.w, r.h, "REVERT", function()
        app.revertSettings()
    end, SMALL))
    add(settings, IRGui.makeButton(c.x, c.y, c.w, c.h, "CLOSE", function()
        app.closeSettings()
    end, SMALL))
    M.report(settings)
end

function M.refreshFavourites(settings)
    if settings.favouritesLabel then
        IRGui.setLabelText(settings.favouritesLabel,
            string.format("FAVOURITES: %d", #sortedKeys(settings.favourites)))
    end
end

-- Takes the dropdown's choice into the model.
function M.poll(settings)
    if not settings.dropdown then
        return
    end
    local selected = IRGui.dropdownSelected(settings.dropdown)
    if selected ~= settings.selected and selected > 0 then
        settings.selected = selected
        settings.editor = settings.editors[selected].name
        M.report(settings)
    end
end

return M
