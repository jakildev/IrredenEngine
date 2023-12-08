#include <irreden/ir_engine.hpp>

#include <irreden/audio/entities/entity_midi_device.hpp>

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: your-creation-here");

    IREngine::init(argc, argv);

    // Initialize entities, command, and systems here
    // ...

    IREngine::gameLoop();

    return 0;
}