add_library(IrredenEngineScripting STATIC
    src/lua_helper.cpp
    src/lua_script.cpp
)

target_compile_options(
    IrredenEngineProfiling
    PUBLIC
    -O3
)

# FetchContent_Declare(
#     lua54
#     URL http://www.lua.org/ftp/lua-5.4.6.tar.gz
# )

# Use precompiled lua54 and include files

target_link_libraries(
    IrredenEngineScripting PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/static/liblua54.a
    IrredenEngineProfiling
)
target_include_directories(
    IrredenEngineScripting
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic/lua54.dll
    ${PROJECT_BINARY_DIR}/lua54.dll COPYONLY
)

# add_custom_command(
#     TARGET IrredenEngineInput
#     POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#         ${glfw_BINARY_DIR}/src/glfw3d.dll
#         ${PROJECT_BINARY_DIR}/glfw3d.dll
# )