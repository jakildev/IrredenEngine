-- ============================================================================
-- palette_selector.lua  --  Interactive palette selection UI
-- ============================================================================

local PS = {}

local SWATCH_SIZE        = ivec3.new(3, 3, 1)
local SWATCH_SPACING     = 1
local ROW_SPACING        = 6
local LABEL_OFFSET_X     = 16
local LABEL_COLOR_NORMAL = {200, 200, 200, 255}
local LABEL_COLOR_HOVER  = {255, 0, 0, 255}
local TITLE_Y            = 12
local PALETTE_START_Y    = 28

function PS.show(palettes, on_select)
    local state = {
        swatch_entities = {},
        label_entities  = {},
        hovered_key     = nil,
        handler_ids     = {},
        entity_to_key   = {},
    }

    state.title_entity = IRText.create("Select Palette", 20, TITLE_Y, {
        color = {255, 255, 255, 255},
        wrapWidth = -1,
    })

    local sorted_keys = {}
    for k, _ in pairs(palettes) do
        sorted_keys[#sorted_keys + 1] = k
    end
    table.sort(sorted_keys)

    local row = 0
    for _, key in ipairs(sorted_keys) do
        local pal = palettes[key]
        local num_colors = #pal
        local base_y = PALETTE_START_Y + row * ROW_SPACING

        local function format_name(s)
            return s:gsub("_", " "):lower():gsub("(%a)([%w]*)",
                function(f, r) return f:upper() .. r end)
        end

        local label = IRText.create(format_name(key), LABEL_OFFSET_X, base_y, {
            color = LABEL_COLOR_NORMAL,
            wrapWidth = -1,
        })
        state.label_entities[key] = label

        local swatch_group = IREntity.createEntityBatchVoxelStatic(
            ivec3.new(num_colors, 1, 1),
            function(params)
                local i = params.index.x
                local x = i * (SWATCH_SIZE.x + SWATCH_SPACING)
                local y = base_y * 0.5
                local z = 0.0
                return C_Position3D.new(vec3.new(x, y, z))
            end,
            function(params)
                local i = params.index.x
                local color = pal[i + 1] or Color.new(128, 128, 128, 255)
                return C_VoxelSetNew.new(SWATCH_SIZE, color)
            end
        )

        for _, entity in ipairs(swatch_group) do
            state.entity_to_key[entity.entity] = key
        end

        state.swatch_entities[key] = swatch_group
        row = row + 1
    end

    state.handler_ids[#state.handler_ids + 1] = IRInput.onEntityHovered(function(entityId)
        local key = state.entity_to_key[entityId]
        print(string.format("[PaletteSelector] onHovered: entityId=%s key=%s",
            tostring(entityId), tostring(key)))
        if not key then return end

        if state.hovered_key and state.hovered_key ~= key then
            PS._unhover_row(state, state.hovered_key)
        end

        state.hovered_key = key
        PS._hover_row(state, key)
    end)

    state.handler_ids[#state.handler_ids + 1] = IRInput.onEntityUnhovered(function(entityId)
        local key = state.entity_to_key[entityId]
        print(string.format("[PaletteSelector] onUnhovered: entityId=%s key=%s",
            tostring(entityId), tostring(key)))
        if not key then return end

        if state.hovered_key == key then
            PS._unhover_row(state, key)
            state.hovered_key = nil
        end
    end)

    state.handler_ids[#state.handler_ids + 1] = IRInput.onEntityClicked(function(entityId, button)
        print(string.format("[PaletteSelector] onClicked: entityId=%s button=%d",
            tostring(entityId), button))
        if button ~= 0 then return end
        local key = state.entity_to_key[entityId]
        if not key then return end

        print("[PaletteSelector] Selected palette: " .. key)
        PS.cleanup(state)
        if on_select then
            on_select(key)
        end
    end)

    print(string.format("[PaletteSelector] Registered %d entity->key mappings, %d handler(s)",
        row, #state.handler_ids))

    state.active = true
    return state
end

function PS._hover_row(state, key)
    local label = state.label_entities[key]
    if label then
        IRText.setColor(label, LABEL_COLOR_HOVER[1], LABEL_COLOR_HOVER[2],
            LABEL_COLOR_HOVER[3], LABEL_COLOR_HOVER[4])
    end
end

function PS._unhover_row(state, key)
    local label = state.label_entities[key]
    if label then
        IRText.setColor(label, LABEL_COLOR_NORMAL[1], LABEL_COLOR_NORMAL[2],
            LABEL_COLOR_NORMAL[3], LABEL_COLOR_NORMAL[4])
    end
end

function PS.cleanup(state)
    for _, handler_id in ipairs(state.handler_ids) do
        IRInput.removeEntityHandler(handler_id)
    end
    state.handler_ids = {}

    for _, group in pairs(state.swatch_entities) do
        for _, entity in ipairs(group) do
            IREntity.destroyEntity(entity)
        end
    end
    state.swatch_entities = {}

    for _, label in pairs(state.label_entities) do
        IRText.remove(label)
    end
    state.label_entities = {}

    if state.title_entity then
        IRText.remove(state.title_entity)
        state.title_entity = nil
    end

    state.entity_to_key = {}
    state.active = false
end

return PS
