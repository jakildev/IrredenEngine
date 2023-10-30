cmake_minimum_required(VERSION 3.24.0)
project(GameBuilds VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(${PROJECT_SOURCE_DIR}/game)
add_subdirectory(${PROJECT_SOURCE_DIR}/engine)

# Eventually move all include files to engine/include
# include_directories(${PROJECT_SOURCE_DIR}/engine/include)