include(GoogleTest)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(IrredenEngineTest
  asset/binary_io_test.cpp
  asset/json_sidecar_test.cpp
  asset/key_value_store_test.cpp
  asset/name_table_test.cpp
  asset/rig_format_test.cpp
  asset/sprite_sheet_test.cpp
  asset/voxel_set_dense_test.cpp
  asset/voxel_set_hybrid_test.cpp
  asset/voxel_set_rle_test.cpp
  asset/voxel_set_shapes_test.cpp
  audio/audio_playback_test.cpp
  audio/midi_multiport_test.cpp
  audio/music_theory_test.cpp
  common/ir_args_test.cpp
  common/ir_platform_test.cpp
  ecs/command_randomize_voxels_test.cpp
  ecs/entity_manager_test.cpp
  ecs/execute_query_test.cpp
  ecs/grid_rotation_test.cpp
  ecs/hitbox_2d_gui_test.cpp
  ecs/joint_matrix_upload_test.cpp
  ecs/modifier_quat_runtime_test.cpp
  ecs/modifier_runtime_test.cpp
  ecs/modifier_types_test.cpp
  ecs/modifier_vec3_runtime_test.cpp
  ecs/propagate_transform_test.cpp
  ecs/rotation_target_test.cpp
  ecs/sort_archetype_relational_test.cpp
  ecs/parallel_spawn_test.cpp
  ecs/skeleton_components_test.cpp
  ecs/skeleton_skinning_test.cpp
  ecs/skinning_pipeline_consistency_test.cpp
  ecs/spatial_grid_test.cpp
  ecs/system_exclude_test.cpp
  ecs/voxel_bone_slot_seed_test.cpp
  ecs/voxel_set_edit_api_test.cpp
  ecs/voxel_set_target_canvas_test.cpp
  math/physics_test.cpp
  math/ir_math_test.cpp
  math/ir_math_viewport_helpers_test.cpp
  math/camera_subpixel_test.cpp
  math/ir_math_color_layout_test.cpp
  math/ir_math_coord_helpers_test.cpp
  math/ir_math_easing_color_sort_test.cpp
  math/ir_math_random_misc_test.cpp
  math/ir_math_trixel_index_test.cpp
  math/ir_math_yaw_deformation_test.cpp
  math/sdf_test.cpp
  render/camera_yaw_test.cpp
  render/detached_world_depth_test.cpp
  render/per_axis_canvas_size_test.cpp
  render/frame_data_sun_layout_test.cpp
  render/gpu_compute_dispatch_test.cpp
  render/per_canvas_light_scope_test.cpp
  render/picking_face_normal_test.cpp
  render/sun_direction_test.cpp
  render/sun_lighting_test.cpp
  render/sprite_animation_test.cpp
  render/sprite_components_test.cpp
  render/sprite_render_test.cpp
  script/lua_audio_midi_observer_test.cpp
  script/lua_collision_bindings_test.cpp
  script/lua_command_test.cpp
  script/lua_input_bindings_test.cpp
  script/lua_component_codegen_test.cpp
  script/lua_component_register_test.cpp
  script/lua_deferred_entity_test.cpp
  script/lua_enum_register_test.cpp
  script/lua_persistence_test.cpp
  script/lua_world_snapshot_test.cpp
  script/lua_pipeline_register_test.cpp
  script/lua_render_bindings_test.cpp
  script/lua_local_transform_lua_test.cpp
  script/lua_rotation_target_lua_test.cpp
  script/lua_system_codegen_test.cpp
  script/prefab_api_test.cpp
  script/lua_system_coexistence_test.cpp
  script/lua_system_register_test.cpp
  script/modifier_lua_test.cpp
  system/pipeline_groups_test.cpp
  system/register_system_test.cpp
  system/system_access_test.cpp
  system/system_concurrency_test.cpp
  time/sim_clock_test.cpp
  time/cycle_test.cpp
  time/timer_test.cpp
  time/stopwatch_test.cpp
  time/lua_sim_test.cpp
  job/ir_job_test.cpp
  utility/ir_utility_test.cpp
  world/chunk_coord_test.cpp
  world/chunk_membership_migration_test.cpp
  world/chunk_persistence_test.cpp
  world/chunk_residency_manager_test.cpp
  world/save_trait_test.cpp
  world/world_snapshot_test.cpp
  world/world_snapshot_relations_test.cpp
  world/persist_round_trip_test.cpp
  world/component_migration_test.cpp
  world/voxel_set_serialize_test.cpp
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

# Headless GPU compute test (render/gpu_compute_dispatch_test.cpp) compiles a
# real engine compute shader by absolute path so it resolves regardless of cwd
# (the engine itself chdir's to the exe dir at boot; the test never does). The
# shader source tree is engine/render/src/shaders/.
target_compile_definitions(IrredenEngineTest PRIVATE
    IR_TEST_RENDER_SHADER_DIR="${CMAKE_SOURCE_DIR}/engine/render/src/shaders"
)

# Build lua_component_codegen_fixtures.hpp from the sibling .lua
# schema and add the include path. The hand-test demo for the codegen
# tool — exercises the full pipeline (Lua schema → C++ struct + Lua
# binding + EntityManager registration) and runs as part of the
# standard test suite.
irreden_lua_codegen(IrredenEngineTest
    SOURCES script/lua_component_codegen_fixtures.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_component_codegen_fixtures.hpp
    DEFAULT_MODE CODEGEN
)

# Schema-error regression: feed the codegen binary a fixture with an
# int32 overflow default in the explicit form (subprocess invocation in
# lua_component_codegen_test.cpp). Path constants are passed via compile
# definitions so the test resolves both at runtime regardless of cwd, and
# the fixture file is added to the dependency graph so an edit triggers
# a test rebuild.
target_compile_definitions(IrredenEngineTest PRIVATE
    IR_LUA_CODEGEN_BINARY="$<TARGET_FILE:ir_lua_codegen>"
    IR_LUA_CODEGEN_OVERFLOW_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/script/lua_component_codegen_overflow_fixture.lua"
    IR_LUA_CODEGEN_ENUM_ERROR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/script/lua_enum_codegen_error_fixture.lua"
)
add_dependencies(IrredenEngineTest ir_lua_codegen)

# Build lua_system_codegen_fixtures.hpp from the sibling .lua fixture.
# The codegen tool processes IRSystem.registerSystem({...}) calls, slices
# the tick function source, parses the DSL subset, and emits typed C++
# tick bodies + a registry helper. The explicit DEFAULT_MODE CODEGEN here
# pins these fixtures to CODEGEN regardless of the IR_LUA_ECS_DEFAULT_MODE
# cache var — these tests verify the typed-emission path and would
# silently skip if the global flag flipped them to EVAL.
irreden_lua_codegen(IrredenEngineTest
    SOURCES script/lua_system_codegen_fixtures.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_system_codegen_fixtures.hpp
    DEFAULT_MODE CODEGEN
)

# Coexistence fixture — one .lua file declaring one CODEGEN system
# (no mode field, follows DEFAULT_MODE) and one EVAL system (explicit
# mode='eval'). Exercises the helper's DEFAULT_MODE param path AND the
# runtime mode-dispatch in IRSystem.registerSystem. The test also loads
# the fixture .lua at runtime, so the path is threaded into the test
# binary as a compile definition so the test source can pass it to
# `LuaScript::scriptFile`.
irreden_lua_codegen(IrredenEngineTest
    SOURCES script/lua_system_coexistence_fixtures.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_system_coexistence_fixtures.hpp
    DEFAULT_MODE CODEGEN
)
target_compile_definitions(IrredenEngineTest PRIVATE
    IR_LUA_COEXIST_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/script/lua_system_coexistence_fixtures.lua"
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
