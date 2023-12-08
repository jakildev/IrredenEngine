set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(YourNewCreation main.cpp)
target_link_libraries(YourNewCreation PUBLIC IrredenEngine)