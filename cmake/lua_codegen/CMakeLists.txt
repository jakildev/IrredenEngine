# cmake/lua_codegen/ — build-time tool that emits C++ component definitions
# from Lua schemas. See main.cpp for design notes; cmake/ir_functions.cmake's
# `irreden_lua_codegen()` helper drives the per-creation invocations.
#
# Linked against the engine's vendored Lua (engine/script/third_party/lua54)
# and sol2 (already fetched by engine/script/CMakeLists.txt). This subdir
# is added AFTER engine/ in the top-level CMakeLists so both deps resolve.

add_executable(ir_lua_codegen main.cpp)
target_link_libraries(ir_lua_codegen PRIVATE lua sol2)
target_compile_features(ir_lua_codegen PRIVATE cxx_std_20)

# Codegen-time errors should surface verbatim; never strip the symbols
# even on Release.
set_target_properties(ir_lua_codegen PROPERTIES
    EXCLUDE_FROM_ALL FALSE
)
