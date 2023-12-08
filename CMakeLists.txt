cmake_minimum_required(VERSION 3.24.0)
project(BuildCreation VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(cmake/ir_functions.cmake)

include_directories(${PROJECT_SOURCE_DIR}/engine/prefabs)

add_subdirectory(${PROJECT_SOURCE_DIR}/engine)
add_subdirectory(${PROJECT_SOURCE_DIR}/creations)
add_subdirectory(${PROJECT_SOURCE_DIR}/test)

# Options you can enable or disable