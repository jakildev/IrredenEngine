-- Unit Movement Demo - Dota 2-style RTS movement
-- Features: A* pathfinding, circle colliders, unit-unit push,
-- turn rate, wall sliding, unit selection, large world

local SCRIPT_DIR = "scripts/"
local function load_module(name)
    return dofile(SCRIPT_DIR .. name .. ".lua")
end

local settings = load_module("settings")
local grid_builder = load_module("grid_builder")
local units = load_module("units")

print("[UnitMovement] Initializing " ..
      settings.grid.size_x .. "x" .. settings.grid.size_y .. " world...")

-- Create level entity with nav grid
grid_builder.create_level(settings)

-- Create grid cells with obstacles
grid_builder.create_flat_grid(settings.grid.size_x, settings.grid.size_y, settings.grid.cell_size)

-- Create player and test units
units.create_units(settings, grid_builder)

-- Start all units selected
IREntity.selectAllUnits()

-- Right-click anywhere: move selected units (or all if none selected)
IRInput.onRightClick(function()
    local targetCell = IREntity.getNavCellAtMouseWorldIso()
    if targetCell then
        IREntity.issueMoveOrderToSelectedUnits(targetCell)
    end
end)

print("[UnitMovement] Init complete. Controls:")
print("  Left-click: single-select")
print("  Left-drag: box select")
print("  Right-click: move selected units")
print("  Tab: toggle screen/iso selection mode")
print("  F6: toggle selection debug logging")
print("  WASD: pan camera")
print("  +/-: zoom")
