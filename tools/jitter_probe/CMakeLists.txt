include(FetchContent)

# stb is already declared by engine/render/CMakeLists.txt (and tools/img_diff);
# the duplicate FetchContent_Declare with the same name is a no-op (CMake reuses
# the cache). Declared here too so jitter_probe can build standalone if a future
# configure ever excludes the engine subdir. GIT_TAG master intentionally
# matches engine/render/CMakeLists.txt — both track the same floating ref.
FetchContent_Declare(
    stbimage
    GIT_REPOSITORY https://github.com/nothings/stb/
    GIT_TAG master
)
FetchContent_MakeAvailable(stbimage)

# IRArgs lives in the engine tree but has no engine dependencies (see the
# dependency-free assert note in engine/ir_args.cpp), so jitter_probe compiles
# the source directly and stays standalone — no link against the engine library.
add_executable(jitter_probe main.cpp ${PROJECT_SOURCE_DIR}/engine/ir_args.cpp)
target_include_directories(jitter_probe PRIVATE
    ${stbimage_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/engine/include
)
