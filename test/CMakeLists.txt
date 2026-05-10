include(GoogleTest)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(IrredenEngineTest
  asset/sprite_sheet_test.cpp
  audio/music_theory_test.cpp
  common/ir_platform_test.cpp
  ecs/entity_manager_test.cpp
  ecs/hitbox_2d_gui_test.cpp
  ecs/modifier_runtime_test.cpp
  ecs/modifier_types_test.cpp
  ecs/system_exclude_test.cpp
  math/physics_test.cpp
  math/ir_math_test.cpp
  math/ir_math_viewport_helpers_test.cpp
  math/ir_math_color_layout_test.cpp
  math/ir_math_coord_helpers_test.cpp
  math/ir_math_easing_color_sort_test.cpp
  math/ir_math_random_misc_test.cpp
  math/ir_math_trixel_index_test.cpp
  math/sdf_test.cpp
  render/camera_yaw_test.cpp
  render/frame_data_sun_layout_test.cpp
  render/per_canvas_light_scope_test.cpp
  render/sun_direction_test.cpp
  render/sun_lighting_test.cpp
  render/sprite_animation_test.cpp
  render/sprite_components_test.cpp
  render/sprite_render_test.cpp
  script/lua_component_codegen_test.cpp
  script/lua_component_register_test.cpp
  script/lua_pipeline_register_test.cpp
  script/lua_system_codegen_test.cpp
  script/lua_system_register_test.cpp
  script/modifier_lua_test.cpp
  system/register_system_test.cpp
  utility/ir_utility_test.cpp
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        main
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
target_link_libraries(IrredenEngineTest PUBLIC
    IrredenEngine
    gtest_main
)

# T-106: build lua_component_codegen_fixtures.hpp from the sibling
# .lua schema and add the include path. The hand-test demo for the
# codegen tool — exercises the full pipeline (Lua schema → C++
# struct + Lua binding + EntityManager registration) and runs as
# part of the standard test suite.
irreden_lua_codegen(IrredenEngineTest
    SOURCES script/lua_component_codegen_fixtures.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_component_codegen_fixtures.hpp
)

# T-106 schema-error regression: feed the codegen binary a fixture with an
# int32 overflow default in the explicit form (subprocess invocation in
# lua_component_codegen_test.cpp). Path constants are passed via compile
# definitions so the test resolves both at runtime regardless of cwd, and
# the fixture file is added to the dependency graph so an edit triggers
# a test rebuild.
target_compile_definitions(IrredenEngineTest PRIVATE
    IR_LUA_CODEGEN_BINARY="$<TARGET_FILE:ir_lua_codegen>"
    IR_LUA_CODEGEN_OVERFLOW_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/script/lua_component_codegen_overflow_fixture.lua"
)
add_dependencies(IrredenEngineTest ir_lua_codegen)

# T-107: build lua_system_codegen_fixtures.hpp from the sibling .lua
# fixture. The codegen tool processes IRSystem.registerSystem({...})
# calls, slices the tick function source, parses the DSL subset, and
# emits typed C++ tick bodies + a registry helper.
irreden_lua_codegen(IrredenEngineTest
    SOURCES script/lua_system_codegen_fixtures.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_system_codegen_fixtures.hpp
)

target_include_directories(IrredenEngineTest PRIVATE
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googlemock/include
)

# Ensure runtime DLL dependencies are available for test discovery/execution.
add_custom_command(
    TARGET IrredenEngineTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IrredenEngineTest> $<TARGET_RUNTIME_DLLS:IrredenEngineTest>
    COMMAND_EXPAND_LISTS
)

gtest_discover_tests(IrredenEngineTest TEST_LIST IrredenEngineTest_targets)
