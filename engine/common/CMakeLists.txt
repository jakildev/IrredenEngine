add_library(IrredenEngineCommon STATIC
    include/irreden/ir_constants.hpp
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