# Included here as well as from engine/render so img_diff still builds if a
# configure ever excludes the engine subdir.
include(${PROJECT_SOURCE_DIR}/cmake/ir_fetch_stb.cmake)

# IRArgs lives in the engine tree but has no engine dependencies (see the
# dependency-free assert note in engine/ir_args.cpp), so img_diff compiles the
# source directly and stays standalone — no link against the engine library.
add_executable(img_diff main.cpp ${PROJECT_SOURCE_DIR}/engine/ir_args.cpp)
target_include_directories(img_diff PRIVATE
    ${stbimage_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/engine/include
)
