FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(IrredenMathBench
    bench_iso_projection.cpp
    bench_sdf.cpp
    bench_trixel.cpp
)

target_link_libraries(IrredenMathBench PRIVATE
    IrredenEngineCommon
    Catch2::Catch2WithMain
)

set_target_properties(IrredenMathBench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
