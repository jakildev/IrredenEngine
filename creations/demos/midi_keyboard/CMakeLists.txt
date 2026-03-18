set(IR_MIDI_KEYBOARD_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR})
add_executable(DemoMidiDevice main.cpp)
target_link_libraries(DemoMidiDevice PUBLIC IrredenEngine)

add_custom_target(DemoMidiDeviceAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/shaders
    COMMENT "Syncing midi_keyboard engine data and shaders"
)

add_dependencies(DemoMidiDevice DemoMidiDeviceAssets)

add_custom_target(DemoMidiDeviceRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MIDI_KEYBOARD_RUNTIME_DIR}/shaders
    COMMAND $<TARGET_FILE:DemoMidiDevice>
    DEPENDS DemoMidiDevice
    WORKING_DIRECTORY ${IR_MIDI_KEYBOARD_RUNTIME_DIR}
    USES_TERMINAL
)