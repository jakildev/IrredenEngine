set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(DefaultCreation main.cpp)

target_link_libraries(DefaultCreation PUBLIC IrredenEngine)