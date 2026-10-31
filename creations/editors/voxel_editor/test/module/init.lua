-- Neutral fixture for IRVoxelEditor --module: five components, five recipes
-- and one panel, so every registration path the editor hosts is exercised and
-- both the COMPONENTS and RECIPES lists hold more rows than they show.

IRComponent.register("TestModuleTag", {
    weight = 1,
    offset = { type = "vec3" },
})

-- More fields than one page of the COMPONENTS field area, which lists them by
-- name.
IRComponent.register("TestModuleWide", {
    a = 1,
    b = 1,
    c = 1,
    d = 1,
    e = 1,
    f = 1,
    g = 1,
})

-- Field names a table constructor cannot spell bare: a reserved word and a
-- punctuated name.
IRComponent.register("TestModuleKeys", {
    ["end"] = 1,
    ["max-value"] = 1,
})

-- The last two module rows: the second lies below the COMPONENTS view, and
-- the editor's own C++ rows follow it.
IRComponent.register("TestModuleSpare", {
    level = 1,
})

IRComponent.register("TestModuleBelow", {
    depth = 1,
})

-- A 1x1 column standing on the seeded ground plane (local z == size.z - 1)
-- at the set's centre; it grows toward -z, the editor's "up". Its `tip`
-- handle sits on the top cell and sets the height from the dragged cell's z.
IREditor.registerRecipe(
    "test_column",
    {
        { name = "height", type = IREditor.ParamType.INT, default = 5, min = 1, max = 8 },
    },
    function(values, size)
        local cx = math.floor(size.x / 2)
        local cy = math.floor(size.y / 2)
        local cells = {}
        for i = 1, values.height do
            cells[#cells + 1] = { cx, cy, size.z - 1 - i, { r = 220, g = 140, b = 60 } }
        end
        return cells
    end,
    {
        handles = {
            {
                name = "tip",
                params = { "height" },
                at = function(values, size)
                    return {
                        math.floor(size.x / 2),
                        math.floor(size.y / 2),
                        size.z - 1 - values.height,
                    }
                end,
                drag = function(values, size, point)
                    return { height = size.z - 1 - point.z }
                end,
            },
        },
    }
)

-- A one-cell-thick square frame on the set's centre layer, around its centre
-- column: the cells whose larger X / Y distance from that column is `half`.
IREditor.registerRecipe(
    "test_frame",
    {
        { name = "half", type = IREditor.ParamType.INT, default = 1, min = 1, max = 4 },
    },
    function(values, size)
        local cx = math.floor(size.x / 2)
        local cy = math.floor(size.y / 2)
        local cz = math.floor(size.z / 2)
        local half = values.half
        local cells = {}
        for dy = -half, half do
            for dx = -half, half do
                if math.max(math.abs(dx), math.abs(dy)) == half then
                    cells[#cells + 1] = { cx + dx, cy + dy, cz, { r = 90, g = 170, b = 230 } }
                end
            end
        end
        return cells
    end
)

-- One cell at the set's centre, on the centre layer.
IREditor.registerRecipe("test_dot", {}, function(values, size)
    return { { math.floor(size.x / 2), math.floor(size.y / 2), math.floor(size.z / 2) } }
end)

-- A 1x1 column like test_column's, two cells off the centre in +X and +Y.
IREditor.registerRecipe("test_post", {}, function(values, size)
    local x = math.floor(size.x / 2) + 2
    local y = math.floor(size.y / 2) + 2
    return { { x, y, size.z - 2 }, { x, y, size.z - 3 } }
end)

-- A 1x1 column two cells off the centre in -X and +Y, `depth` cells tall; the
-- fifth recipe, so it lies below the RECIPES view.
IREditor.registerRecipe(
    "test_below",
    {
        { name = "depth", type = IREditor.ParamType.INT, default = 1, min = 1, max = 4 },
    },
    function(values, size)
        local x = math.floor(size.x / 2) - 2
        local y = math.floor(size.y / 2) + 2
        local cells = {}
        for i = 1, values.depth do
            cells[#cells + 1] = { x, y, size.z - 1 - i, { r = 120, g = 200, b = 90 } }
        end
        return cells
    end
)

IREditor.registerPanel("TestModulePanel", function(x, y, w, h)
    IRGui.makeLabel(x + 4, y + 18, "MODULE LOADED")
    IRGui.makeLabel(x + 4, y + 32, "FIELD ROW")
end)

IREditor.registerSession("test_parts", function(session)
    local size = session:sceneSize()
    local center = {
        x = math.floor(size.x / 2),
        y = math.floor(size.y / 2),
        z = math.floor(size.z / 2),
    }
    local blob_front = { center.x, center.y, center.z - 2 }
    local painted = { center.x, center.y, center.z - 3 }

    session:segment("bake")
    session:addVoxelPart()
    session:bakeSphere(1.9)
    session:expectPartOccupancy(0, blob_front, true, "blob_baked")

    session:segment("before_click")
    session:expectPartOccupancy(0, painted, false, "before_click_empty")
    session:segment("click")
    session:click(painted)
    session:expectPartOccupancy(0, painted, true, "clicked_cell_filled")

    session:segment("array_and_band")
    session:applyRadialArray(2)
    session:dragLodSlider(IREditor.SliderTarget.LOD_FINE, 3)
    session:dragLodSlider(IREditor.SliderTarget.LOD_TIER, 0)
    session:expectPartCount(3, "three_parts_before_save")
    session:expectPartGated(2, true, 0, "selected_copy_band_set")

    session:segment("save")
    session:save()
    session:segment("clear")
    session:clearEntityScene()
    session:segment("reload")
    session:reload()
    session:expectPartOccupancy(0, painted, true, "reload_occupancy")
    session:expectPartCount(3, "reload_part_count")
    session:expectPartGated(2, true, 0, "reload_band")
end)

IREditor.registerSession("test_recipe_attach", function(session)
    local size = session:sceneSize()
    local x = math.floor(size.x / 2)
    local y = math.floor(size.y / 2)
    local recipe_cell = { x, y, size.z - 4 }
    local click_target = { x, y, size.z - 5 }

    session:segment("apply")
    session:addVoxelPart()
    session:expectComponentValue(
        0, "TestModuleTag", "weight", nil, true, "component_absent_before_attach")
    session:applyRecipe("test_column", { height = 3 })
    session:expectPartOccupancy(0, recipe_cell, true, "recipe_cell_filled")
    session:expectPartOccupancy(0, click_target, false, "click_target_empty_before_click")

    session:segment("click")
    session:click(click_target)
    session:expectPartOccupancy(0, click_target, true, "click_target_filled")

    session:segment("component")
    session:attachComponent("TestModuleTag")
    session:setComponentField("TestModuleTag", "weight", "7")
    session:expectComponentValue(0, "TestModuleTag", "weight", 7, true, "field_set_to_seven")

    session:segment("save")
    session:save()
    session:segment("clear")
    session:clearEntityScene()
    session:segment("reload")
    session:reload()
    session:expectPartOccupancy(0, click_target, true, "clicked_cell_survives_reload")
    session:expectComponentValue(0, "TestModuleTag", "weight", 7, true, "field_survives_reload")
    session:expectComponentValue(
        0, "TestModuleTag", "weight", 1, false, "field_is_not_default_after_reload")
end)

-- Drags test_column's tip handle on a part: it lengthens the column, shortens
-- it (clearing a cell the lengthening made and one APPLY made, which an
-- additive re-apply would keep), clamps a drag past max, and undoes and redoes
-- the last drag. The handle exists only in transform mode, and at height 8 it
-- sits under the part's gizmo, which still owns every pixel it had before the
-- handle existed.
IREditor.registerSession("test_recipe_handle", function(session)
    local size = session:sceneSize()
    local x = math.floor(size.x / 2)
    local y = math.floor(size.y / 2)
    local function top(height)
        return { x, y, size.z - 1 - height }
    end
    -- A target keeps the coordinate sum of the handle's point, so it lies on
    -- the screen plane the drag is read on: each cell it moves in z it moves
    -- back in x.
    local function onPlane(height, newHeight)
        local from = top(height)
        local dz = height - newHeight
        return { from[1] - dz, from[2], from[3] + dz }
    end

    session:segment("gizmo_baseline")
    session:addVoxelPart()
    session:toggleTransformMode()
    session:expectGizmoHandleTexels("part_gizmo_before_any_handle")

    session:segment("apply")
    session:toggleTransformMode()
    session:applyRecipe("test_column", { height = 3 })
    session:expectRecipeValue(0, "height", 3, "applied_height")
    session:expectPartMatchesRecipe(0, "applied_cells_match")
    session:expectRecipeHandleTexels("tip", false, "no_handle_outside_transform_mode")

    session:segment("handle_drawn")
    session:toggleTransformMode()
    session:expectRecipeHandleTexels("tip", true, "handle_drawn_in_transform_mode")
    session:expectPartOccupancy(0, top(4), false, "cell_above_tip_empty_before_drag")

    session:segment("lengthen")
    session:dragRecipeHandle("tip", onPlane(3, 4))
    session:expectRecipeValue(0, "height", 4, "lengthened_height")
    session:expectPartMatchesRecipe(0, "lengthened_cells_match")
    session:expectPartOccupancy(0, top(4), true, "cell_above_old_tip_filled")

    session:segment("shorten")
    session:dragRecipeHandle("tip", onPlane(4, 1))
    session:expectRecipeValue(0, "height", 1, "shortened_height")
    session:expectPartMatchesRecipe(0, "shortened_cells_match")
    session:expectPartOccupancy(0, top(4), false, "cell_the_drag_filled_cleared")
    session:expectPartOccupancy(0, top(3), false, "cell_apply_filled_cleared")

    session:segment("past_max")
    session:dragRecipeHandle("tip", onPlane(1, 10))
    session:expectRecipeValue(0, "height", 8, "height_clamped_to_max")
    session:expectPartMatchesRecipe(0, "clamped_cells_match")
    session:expectRecipeHandleTexels("tip", true, "handle_drawn_under_the_gizmo")
    session:expectGizmoHandleTexelsUnchanged("handle_steals_no_gizmo_pixel")

    session:segment("undo")
    session:chordKey(IRInput.Key.LEFT_CONTROL, IRInput.Key.Z)
    session:expectRecipeValue(0, "height", 1, "undo_restores_height")
    session:expectPartMatchesRecipe(0, "undo_restores_cells")
    session:expectPartOccupancy(0, top(5), false, "undo_clears_dragged_cell")

    session:segment("redo")
    session:chordKey(IRInput.Key.LEFT_CONTROL, IRInput.Key.Y)
    session:expectRecipeValue(0, "height", 8, "redo_restores_height")
    session:expectPartMatchesRecipe(0, "redo_restores_cells")
    session:expectPartOccupancy(0, top(5), true, "redo_refills_dragged_cell")
end)

-- Reaches rows below both list views by name alone: the last recipe, the last
-- module component, and the C++ C_AutoSpin row after it.
IREditor.registerSession("test_list_scroll", function(session)
    local size = session:sceneSize()
    local x = math.floor(size.x / 2) - 2
    local y = math.floor(size.y / 2) + 2
    local recipe_top = { x, y, size.z - 4 }
    local spin = 0.25

    session:segment("arm")
    session:addVoxelPart()
    session:expectPartOccupancy(0, recipe_top, false, "below_view_recipe_cell_empty_before_apply")
    session:expectComponentValue(
        0, "TestModuleBelow", "depth", nil, true, "below_view_component_absent_before_attach")
    session:expectPartSpinRate(0, spin, false, "spin_absent_before_attach")

    session:segment("recipe")
    session:applyRecipe("test_below", { depth = 3 })
    session:expectPartOccupancy(0, recipe_top, true, "below_view_recipe_cell_filled")

    session:segment("module_component")
    session:attachComponent("TestModuleBelow")
    session:setComponentField("TestModuleBelow", "depth", "6")
    session:expectComponentValue(0, "TestModuleBelow", "depth", 6, true, "below_view_field_set")

    session:segment("engine_component")
    session:attachComponent("C_AutoSpin")
    session:setComponentOverrides(
        "C_AutoSpin", "{ axis = { 0, 0, 1 }, radiansPerFrame = " .. spin .. " }")
    session:expectPartSpinRate(0, spin, true, "engine_row_spin_set")

    session:segment("save")
    session:save()
    session:segment("clear")
    session:clearEntityScene()
    session:segment("reload")
    session:reload()
    session:expectPartOccupancy(0, recipe_top, true, "below_view_recipe_cell_survives_reload")
    session:expectComponentValue(
        0, "TestModuleBelow", "depth", 6, true, "below_view_field_survives_reload")
    session:expectPartSpinRate(0, spin, true, "engine_row_spin_survives_reload")
end)

IREditor.registerSession("test_single", function(session)
    local size = session:sceneSize()
    local cell = { math.floor(size.x / 2), math.floor(size.y / 2), size.z - 2 }
    session:segment("place")
    session:click(cell)
    session:expectOccupancy(cell, true, "single_cell_placed")
    session:segment("save")
    session:save()
end)

IREditor.registerSession("test_radius", function(session)
    local size = session:sceneSize()
    local pivot = {
        x = session:sceneOrigin().x + (size.x - 1) / 2,
        y = session:sceneOrigin().y + (size.y - 1) / 2,
        z = session:sceneOrigin().z + (size.z - 1) / 2,
    }
    local blob_front = {
        math.floor(size.x / 2),
        math.floor(size.y / 2),
        math.floor(size.z / 2) - 2,
    }
    local painted = { blob_front[1] + 1, blob_front[2], blob_front[3] - 1 }

    session:segment("bake")
    session:addVoxelPart()
    session:bakeSphere(1.9)

    session:segment("radius_array")
    session:applyRadialArray(2, 8)
    session:expectPartTransform(
        2, { pivot.x - 8, pivot.y, pivot.z }, 0.001, true, "copy_one_at_radius_8"
    )
    session:expectPartOccupancy(2, blob_front, true, "copy_blob_at_radius_8")
    session:click(painted)
    session:expectPartOccupancy(2, painted, true, "copy_cell_at_radius_8")
end)

IREditor.registerSession("test_yaw", function(session)
    session:segment("bake")
    session:addVoxelPart()
    session:bakeSphere(1.9)

    session:segment("yaw_array")
    session:applyRadialArray(3, 4, 60)
    session:expectPartYaw(2, 60, 0.01, "copy_one_yaw_60")
    session:expectPartYaw(3, 120, 0.01, "copy_two_yaw_120")
end)

IREditor.registerSession("test_unreachable", function(session)
    session:click({ 0, 0, 0 })
end)

-- At the default scene size the BAKE slider can hold a radius either side of
-- 2.0981 for this request, where the sphere gains 32 cells, (7, 7, 5) among
-- them. bakeSphere refuses it, so the part stays clear and the click has
-- nothing to pick.
IREditor.registerSession("test_bake_radius_guard", function(session)
    session:segment("bake")
    session:addVoxelPart()
    session:bakeSphere(2.095)
    session:segment("click")
    session:click({ 7, 7, 5 })
    session:segment("save")
    session:save()
end)
