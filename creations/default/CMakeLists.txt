set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# include_directories(${PROJECT_SOURCE_DIR}/irreden-engine/src)

# set(IrredenEngine_DIR ${CMAKE_SOURCE_DIR}/../irreden-engine)

# Not sure if this is good practice
# set(DLL_DIRECTORY ${PROJECT_BINARY_DIR}/engine)
# set(ENV{PATH} "${DLL_DIRECTORY};$ENV{PATH}")

# add_subdirectory(../irreden-engine engine)
# include_directories(${IrredenEngine_DIR}/src)
# include_directories(${IrredenEngine_DIR}/src/include)

add_executable(DefaultCreation main.cpp)

target_link_libraries(DefaultCreation PUBLIC IrredenEngine)

