-- What IRVoxelEditor's module_loaded session asserts for the module in this
-- directory. The session reads it from <module dir>/session_expect.lua, so the
-- editor's own source names no module content.
--
--   components: registered component name + field count, read back through
--               the registry enumeration
--   panels:     panel name + a label its build function makes
--   recipe:     the recipe to apply and the slider values to drag first; every
--               value must differ from its default so the drag is proven
--
-- component_attach reads componentAttach: the component to attach to part 0,
-- the field to type into, the value typed, and the field's default, which the
-- value must differ from so the typing is proven.
--
-- component_field_page reads componentFieldPage, the same shape, for a field
-- past the first page of the field area (fields are listed by name).
--
-- component_field_key reads componentFieldKey, the same shape, for a field
-- whose name is not a Lua identifier.
--
-- composite_proof reads compositeProof: the recipe that authors its stem part
-- and the one that authors its frame part, each { name, values } as `recipe`
-- is. The session sizes its parts so the stem's top cell is the cell the
-- frame rings.
return {
    components = {
        { name = "TestModuleTag", fieldCount = 2 },
        { name = "TestModuleWide", fieldCount = 7 },
        { name = "TestModuleKeys", fieldCount = 2 },
        { name = "TestModuleSpare", fieldCount = 1 },
        { name = "TestModuleBelow", fieldCount = 1 },
    },
    panels = {
        { name = "TestModulePanel", label = "MODULE LOADED" },
    },
    recipe = {
        name = "test_column",
        values = { height = 3 },
    },
    componentAttach = {
        component = "TestModuleTag",
        field = "weight",
        value = 7,
        default = 1,
    },
    componentFieldPage = {
        component = "TestModuleWide",
        field = "g",
        value = 7,
        default = 1,
    },
    componentFieldKey = {
        component = "TestModuleKeys",
        field = "end",
        value = 7,
        default = 1,
    },
    compositeProof = {
        stem = { name = "test_column", values = { height = 6 } },
        frame = { name = "test_frame", values = { half = 2 } },
    },
    moduleArg = "profile=alpha beta  gamma=2",
}
