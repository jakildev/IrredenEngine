add_library(IrredenEngineTime STATIC
    src/time_manager.cpp
    src/ir_time.cpp
)

target_compile_options(
    IrredenEngineTime
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineTime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    IrredenEngineTime PUBLIC
    IrredenEngineMath
    IrredenEngineCommon
    IrredenEngineProfile
)