include(GoogleTest)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(IrredenEngineTest
  ecs/entity_manager_test.cpp
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
