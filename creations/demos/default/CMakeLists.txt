set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRCreationDefault
    main_lua.cpp
    lua_bindings.cpp
)
# add_executable(IRCreationDefault main.cpp)
target_link_libraries(IRCreationDefault PUBLIC IrredenEngine)

add_custom_command(TARGET IRCreationDefault POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/main.lua ${PROJECT_BINARY_DIR}/main.lua
)

add_custom_command(TARGET IRCreationDefault POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${PROJECT_BINARY_DIR}/config.lua
)