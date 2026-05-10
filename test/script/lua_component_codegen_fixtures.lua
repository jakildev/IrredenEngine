-- T-106 regression fixture: declares the same shapes the EVAL test
-- (lua_component_register_test.cpp) exercises via inline `safe_script`
-- strings, but as a real .lua schema processed by cmake/lua_codegen.
-- The generated header (lua_component_codegen_fixtures.hpp) is included
-- by lua_component_codegen_test.cpp, which mirrors the EVAL test's
-- expectations against the codegen'd structs.
--
-- Names are prefixed `Codegen` so they don't collide with IRComponents
-- types declared elsewhere in the engine.

IRComponent.register("CodegenHp", {
    current = 100,
    max = 100,
})

-- Non-integer float defaults (e.g. 1.5) are required so the LuaJIT compat
-- shim's lua_isinteger correctly identifies them as float. Whole-number
-- defaults like 0.0 are indistinguishable from 0 at the C level under LuaJIT;
-- use `{ type = "float", default = 0 }` for those instead.
IRComponent.register("CodegenVel", {
    x = 1.5,
    y = 1.5,
    z = 1.5,
})

IRComponent.register("CodegenMixed", {
    tag = "hello",
    alive = true,
    count = 42,
    weight = 3.5,
})

IRComponent.register("CodegenHpForced", {
    current = { type = "float", default = 100 },
})

IRComponent.register("CodegenScore", {
    value = 0,
})
