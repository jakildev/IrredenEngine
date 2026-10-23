set(IR_MIDI_KEYBOARD_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR})
add_executable(IRMidiDevice main.cpp)
target_link_libraries(IRMidiDevice PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRMidiDevice)

add_custom_target(IRMidiDeviceRun
    COMMAND $<TARGET_FILE:IRMidiDevice>
    DEPENDS IRMidiDevice IRMidiDeviceAssets
    WORKING_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR}
    USES_TERMINAL
)
