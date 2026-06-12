-- #1403 build-time error fixture: a non-string enum member must abort the
-- codegen tool with a clear diagnostic (the CODEGEN mirror of the EVAL
-- NonStringMemberRaises test). Spawned by the LuaEnumCodegenSchemaError test
-- in lua_component_codegen_test.cpp; never wired into a build target.
IREnum.register("BadEnum", { "OK", 42 })
