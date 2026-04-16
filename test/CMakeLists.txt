include(GoogleTest)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(IrredenEngineTest
  audio/music_theory_test.cpp
  common/ir_platform_test.cpp
  ecs/entity_manager_test.cpp
  math/physics_test.cpp
  math/ir_math_test.cpp
  math/ir_math_viewport_helpers_test.cpp
  math/ir_math_color_layout_test.cpp
  math/ir_math_coord_helpers_test.cpp
  math/ir_math_easing_color_sort_test.cpp
  math/ir_math_random_misc_test.cpp
  math/ir_math_trixel_index_test.cpp
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
