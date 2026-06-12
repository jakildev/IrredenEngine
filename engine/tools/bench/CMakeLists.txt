add_executable(ir_ref_bench
    ir_ref_bench.cpp
)

target_link_libraries(ir_ref_bench PRIVATE
    IrredenEngineMath
)

target_compile_options(ir_ref_bench PRIVATE -O3)

set_target_properties(ir_ref_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
