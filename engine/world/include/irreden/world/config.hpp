#ifndef CONFIG_H
#define CONFIG_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IREngine {

    struct WorldConfig {
        ivec2 gameResolution_ = ivec2(1920, 1080);
        ivec2 initWindowSize_ = ivec2(1920, 1080);
    };

    constexpr WorldConfig kConfigDefaultHorizontal = {
        .gameResolution_ = ivec2(1920, 1080),
        .initWindowSize_ = ivec2(1920, 1080)
    };

    constexpr WorldConfig kConfigDefaultVertical = {
        .gameResolution_ = ivec2(1080, 1920) / ivec2(2),
        .initWindowSize_ = ivec2(1080, 1920) / ivec2(2)
    };

};

#endif /* CONFIG_H */
