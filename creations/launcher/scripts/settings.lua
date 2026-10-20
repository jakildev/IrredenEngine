-- Launcher settings: the editor entry, favourites, collapsed groups and the
-- last preset per target, persisted through IRSave in the run directory's
-- save_files/<store>.irkv. The editor is an entry name from the engine's
-- fixed table, never a command; a stored name that is not found on PATH
-- falls back to the first entry that is.

local L = require("launcher_layout")
local WidgetList = require("widget_list")

local M = {}

local SMALL = IRText.Size.SMALL

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
        layout = "",
        widgets = {},
        open = false,
    }
end

function M.load(settings)
    local store = settings.store
    IRSave.load(store)
    settings.editor = IRSave.get(store, "editor", "")
    settings.favourites = listToSet(IRSave.get(store, "favourites"))
    settings.collapsed = listToSet(IRSave.get(store, "collapsed"))
    settings.presets = {}
    settings.layout = IRSave.get(store, "layout", "")
    local presets = IRSave.get(store, "presets")
    if type(presets) == "table" then
        for index = 1, #presets do
            local target, preset = presets[index]:match("^(.+)=(%d+)$")
            if target then
                settings.presets[target] = tonumber(preset)
            end
        end
    end
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
    settings.selected = selected
    if selected > 0 then
        settings.editor = settings.editors[selected].name
    end
    settings.favouritesLabel = add(settings, IRGui.makeLabel(L.settings.favourites.x,
        L.settings.favourites.y, "", nil, SMALL))
    M.refreshFavourites(settings)
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
