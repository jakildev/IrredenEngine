set(IR_ANALYTIC_ORACLE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_ANALYTIC_ORACLE_RUNTIME_DIR})
add_executable(IRAnalyticOracle main.cpp)
target_link_libraries(IRAnalyticOracle PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRAnalyticOracle SCRIPTS config.lua)

add_custom_target(IRAnalyticOracleRun
    COMMAND $<TARGET_FILE:IRAnalyticOracle>
    DEPENDS IRAnalyticOracle IRAnalyticOracleAssets
    WORKING_DIRECTORY ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}
    USES_TERMINAL
)
