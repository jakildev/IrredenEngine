include(FetchContent)

# stb is already declared by engine/render/CMakeLists.txt; the second
# FetchContent_Declare with the same name is a no-op (CMake reuses the cache).
# Declared here too so img_diff can build standalone if a future configure
# ever excludes the engine subdir.
FetchContent_Declare(
    stbimage
    GIT_REPOSITORY https://github.com/nothings/stb/
    GIT_TAG master
)
FetchContent_MakeAvailable(stbimage)

add_executable(img_diff main.cpp)
target_include_directories(img_diff PRIVATE ${stbimage_SOURCE_DIR})
