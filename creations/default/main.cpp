#include "world.hpp"
int main(int argc, char **argv) {
    IR_LOG_INFO("Starting game: demo (your-game-here)");

    World gameworld = World{argc, argv};

    gameworld.gameLoop();

    return 0;
}
