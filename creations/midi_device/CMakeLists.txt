set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(DemoMidiDevice main.cpp)
target_link_libraries(DemoMidiDevice PUBLIC IrredenEngine)