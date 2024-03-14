set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(Grower main.cpp)
target_link_libraries(Grower PUBLIC IrredenEngine)