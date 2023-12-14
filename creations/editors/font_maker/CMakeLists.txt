set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(EditorFontMaker main.cpp)
target_link_libraries(EditorFontMaker PUBLIC IrredenEngine)