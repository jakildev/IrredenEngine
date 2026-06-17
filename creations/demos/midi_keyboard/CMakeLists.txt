set(IR_MIDI_KEYBOARD_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR})
add_executable(DemoMidiDevice main.cpp)
target_link_libraries(DemoMidiDevice PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(DemoMidiDevice)

add_custom_target(DemoMidiDeviceRun
    COMMAND $<TARGET_FILE:DemoMidiDevice>
    DEPENDS DemoMidiDevice DemoMidiDeviceAssets
    WORKING_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR}
    USES_TERMINAL
)
