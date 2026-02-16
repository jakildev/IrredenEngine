#include <irreden/ir_engine.hpp>

#include <irreden/audio/entities/entity_midi_device.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: your-creation-here");

    IREngine::init();

    // Initialize entities, command, and systems here
    // ...
    IREntity::createEntity(
        C_Position3D{
            0, 0, 0
        },
        C_VoxelSetNew{
            ivec3(4, 4, 4),
            Color{150, 100, 50, 255}
        }
    );
    IREntity::createEntity<IREntity::kMidiDevice>(
        "UMC1820 MIDI In",
        MidiDeviceType::MIDI_DEVICE_TYPE_IN
    );
    IREntity::createEntity<IREntity::kMidiDevice>(
        "UMC1820 MIDI Out",
        MidiDeviceType::MIDI_DEVICE_TYPE_OUT
    );

    IREngine::gameLoop();

    return 0;
}