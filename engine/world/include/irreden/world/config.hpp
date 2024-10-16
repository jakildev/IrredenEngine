#ifndef CONFIG_H
#define CONFIG_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>

#include <string>

using namespace IRMath;

namespace IREngine {

    class WorldConfigNew {
    public:
        WorldConfigNew(
            IRScript::ConfigOption<IRScript::TABLE> config
        )
        {

        }
        ConfigOption<IRScript::NUMBER> gameResolutionInt_ = 
            ConfigOption<IRScript::NUMBER>(
                "game_resolution_x",
                1920
            );
    private:
        ivec2 gameResolution_;
        ivec2 initWindowSize_;
    };

    struct WorldConfig {
        ivec2 gameResolution_ = ivec2(1920, 1080);
        ivec2 initWindowSize_ = ivec2(1920, 1080);
    };

    constexpr WorldConfig kConfigDefaultHorizontal = {
        .gameResolution_ = ivec2(1920, 1080),
        .initWindowSize_ = ivec2(1920, 1080)
    };
    constexpr WorldConfig kConfigDefaultHorizontalSmall = {
        .gameResolution_ = ivec2(1920, 1080) / ivec2(2),
        .initWindowSize_ = ivec2(1920, 1080) / ivec2(2)
    };
    constexpr WorldConfig kConfigHorizontalLowResolution = {
        .gameResolution_ = ivec2(1920, 1080) / ivec2(4),
        .initWindowSize_ = ivec2(1920, 1080)
    };

    constexpr WorldConfig kConfigDefaultVertical = {
        .gameResolution_ = ivec2(1080, 1920) / ivec2(2),
        .initWindowSize_ = ivec2(1080, 1920) / ivec2(2)
    };

};

#endif /* CONFIG_H */
