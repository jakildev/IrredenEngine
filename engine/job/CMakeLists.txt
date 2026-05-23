add_library(IrredenEngineJob STATIC
    src/job_manager.cpp
    src/ir_job.cpp
)

add_subdirectory(third_party/enkiTS)

target_link_libraries(IrredenEngineJob PUBLIC
    IrredenEngineProfile
    IrredenEngineMath
    enkits
)

target_include_directories(IrredenEngineJob PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
