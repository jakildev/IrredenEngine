-- What IRVoxelEditor's module_loaded session asserts for the module in this
-- directory. The session reads it from <module dir>/session_expect.lua, so the
-- editor's own source names no module content.
--
--   components: registered component name + field count, read back through
--               the registry enumeration
--   panels:     panel name + the one label its build function makes
--   recipe:     the recipe to apply and the slider values to drag first; every
--               value must differ from its default so the drag is proven
return {
    components = {
        { name = "TestModuleTag", fieldCount = 2 },
    },
    panels = {
        { name = "TestModulePanel", label = "MODULE LOADED" },
    },
    recipe = {
        name = "test_column",
        values = { height = 3 },
    },
}
