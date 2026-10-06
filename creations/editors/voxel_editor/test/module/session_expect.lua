-- What IRVoxelEditor's module_loaded session asserts for the module in this
-- directory. The session reads it from <module dir>/session_expect.lua, so the
-- editor's own source names no module content.
--
--   components: registered component name + field count, read back through
--               the registry enumeration
--   panels:     panel name + the one label its build function makes
--   recipe:     the recipe to apply and the slider values to drag first; every
--               value must differ from its default so the drag is proven
--
-- component_attach reads componentAttach: the component to attach to part 0,
-- the field to type into, the value typed, and the field's default, which the
-- value must differ from so the typing is proven.
--
-- component_field_page reads componentFieldPage: a component with more fields
-- than one page of the field area, the value typed into its last reflected
-- field, and the default every field of it shares.
return {
    components = {
        { name = "TestModuleTag", fieldCount = 2 },
        { name = "TestModuleWide", fieldCount = 7 },
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
        value = 7,
        default = 1,
    },
}
