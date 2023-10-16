#include "world.hpp"
#include <engine.hpp>
int main(int argc, char **argv) {
    GAME_LOG_INFO("Starting game: demo (your-game-here)");

    World gameworld = World{argc, argv};

    // hmmmmm, multiple worlds at once??
    gameworld.gameLoop();

    return 0;
}
