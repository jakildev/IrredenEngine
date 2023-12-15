set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRCreationDefault main.cpp)
target_link_libraries(IRCreationDefault PUBLIC IrredenEngine)