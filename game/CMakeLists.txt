set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# include_directories(${PROJECT_SOURCE_DIR}/irreden-engine/src)

# set(IrredenEngine_DIR ${CMAKE_SOURCE_DIR}/../irreden-engine)

# Not sure if this is good practice
# set(DLL_DIRECTORY ${PROJECT_BINARY_DIR}/engine)
# set(ENV{PATH} "${DLL_DIRECTORY};$ENV{PATH}")

# add_subdirectory(../irreden-engine engine)
# include_directories(${IrredenEngine_DIR}/src)
# include_directories(${IrredenEngine_DIR}/src/include)

add_executable(UserProject world.cpp main.cpp)

target_include_directories(
    UserProject
    PUBLIC
    ${PROJECT_SOURCE_DIR}/engine/src
)
target_link_libraries(UserProject PUBLIC IrredenEngine)

