#ifndef LIGHTING_DEMO_MAIN_H
#define LIGHTING_DEMO_MAIN_H

#include "common/lighting_demo_scene.hpp"

#define IR_LIGHTING_DEMO_MAIN(...)                                                                 \
    int main(int argc, char **argv) {                                                              \
        return IRLightingDemo::run(argc, argv, __VA_ARGS__);                                       \
    }

#endif /* LIGHTING_DEMO_MAIN_H */
