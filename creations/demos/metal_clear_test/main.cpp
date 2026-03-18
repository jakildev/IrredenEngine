#include <irreden/ir_engine.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: metal_clear_test");

    IREngine::init(argv[0], "config.lua");

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
    );

    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );

    IREngine::gameLoop();
    return 0;
}
