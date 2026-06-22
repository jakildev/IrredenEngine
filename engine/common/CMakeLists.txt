# Header-only module (constants + compile-time platform detection); see this
# directory's CLAUDE.md. With no real translation unit, a STATIC build emitted
# an empty archive ("ranlib: … table of contents is empty"). INTERFACE is the
# honest shape: consumers get the include dir and IRMath transitively.
add_library(IrredenEngineCommon INTERFACE)

target_link_libraries(
    IrredenEngineCommon INTERFACE
    IrredenEngineMath
)

target_include_directories(
    IrredenEngineCommon
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
