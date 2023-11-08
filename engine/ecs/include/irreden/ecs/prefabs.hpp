/*
 * Project: Irreden Engine
 * File: prefabs.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 * -----
 * Copyright
 */

#ifndef PREFABS_H
#define PREFABS_H

namespace IRECS {

    enum PrefabTypes {
        kExample,
        kGLFWJoystick,
        kPlayer,
        kVoxelSprite,
        kTestBlock,
        kSingleVoxel,
        kMidiDevice,
        kMidiMessageOut,
        kCamera,
        kVoxelParticle,
        kMidiSequenceAnimated,
        kKeyMouseButton,
        kMouseScroll,
        kCanvas
    };

    template<PrefabTypes type>
    struct Prefab;

    // struct Prefab {
    //     template<PrefabTypes type, typename... Args>
    //     static EntityHandle create(Args&&... args);
    // };

    // template<PrefabTypes type, typename... Args>
    // EntityHandle create(Args&&... args);
} // namespace IRECS

#endif /* PREFABS_H */
