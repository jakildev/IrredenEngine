add_library(IrredenEngineUtility STATIC
    src/ir_utility.cpp
)

target_compile_options(
    IrredenEngineUtility
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineUtility
    PUBLIC
    IrredenEngineProfile
)

target_include_directories(
    IrredenEngineUtility
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
