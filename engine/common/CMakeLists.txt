add_library(IrredenEngineCommon STATIC
    src/ir_constants.cpp
)

target_link_libraries(
    IrredenEngineCommon PUBLIC
    IrredenEngineMath
)

target_include_directories(
    IrredenEngineCommon
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)