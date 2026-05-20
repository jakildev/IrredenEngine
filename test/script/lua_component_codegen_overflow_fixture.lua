-- Negative-path fixture for the int32 overflow guard in
-- inferFromExplicitTable. Loaded by the codegen binary as a subprocess
-- in the ExplicitInt32OverflowRaisesError test, NOT by irreden_lua_codegen
-- (which would fail the build). The codegen tool must reject this with a
-- "out of int32 range" diagnostic and a non-zero exit.

IRComponent.register("CodegenOverflow", {
    value = { type = "int32", default = 2147483648 },
})
