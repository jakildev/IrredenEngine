#include <irreden/ir_engine.hpp>

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: your-creation-here");

    IREngine::init(argc, argv);

    // Initialize entities, command, and systems here
    // ...

    IREngine::gameLoop();

    return 0;
}