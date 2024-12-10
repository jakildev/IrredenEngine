add_library(IrredenEngineScripting STATIC
    src/lua_script.cpp
)

add_subdirectory(third_party/lua54)

target_compile_options(
    IrredenEngineProfile
    PUBLIC
    -O3
)

# FetchContent_Declare(
#     lua54
#     URL http://www.lua.org/ftp/lua-5.4.6.tar.gz
# )

# Use precompiled lua54 and include files

# target_link_libraries(
#     IrredenEngineScripting PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/lib/static/liblua54.a
#     IrredenEngineProfile
# )
# target_include_directories(
#     IrredenEngineScripting
#     PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/lua54
# )

# IR_copyDLL(
#     IrredenEngineScripting
#     lua54
#     ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic
# )

# configure_file(
#     ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic/lua54.dll
#     ${PROJECT_BINARY_DIR}/lua54.dll COPYONLY
# )

# Use sol2 library for C++ binding
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG main
)
FetchContent_MakeAvailable(sol2)
target_link_libraries(
    IrredenEngineScripting PUBLIC
    lua
    sol2
    IrredenEngineEntity
)
target_include_directories(
    IrredenEngineScripting
    PUBLIC
    ${sol2_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# add_custom_command(
#     TARGET IrredenEngineInput
#     POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#         ${glfw_BINARY_DIR}/src/glfw3d.dll
#         ${PROJECT_BINARY_DIR}/glfw3d.dll
# )