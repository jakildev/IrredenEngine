set(IR_PERSIST_ROUNDTRIP_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_PERSIST_ROUNDTRIP_RUNTIME_DIR})
add_executable(IRPersistRoundtrip main.cpp)
target_link_libraries(IRPersistRoundtrip PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs) and the
# one-command bundle target IRPersistRoundtripPackage. See cmake/ir_functions.cmake.
irreden_bundle_assets(IRPersistRoundtrip SCRIPTS config.lua)
irreden_package_target(IRPersistRoundtrip)

# Build + launch from the exe directory (assets land via IRPersistRoundtripAssets).
add_custom_target(IRPersistRoundtripRun
    COMMAND $<TARGET_FILE:IRPersistRoundtrip>
    DEPENDS IRPersistRoundtrip IRPersistRoundtripAssets
    WORKING_DIRECTORY ${IR_PERSIST_ROUNDTRIP_RUNTIME_DIR}
    USES_TERMINAL
)
