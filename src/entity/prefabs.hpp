#ifndef PREFABS_H
#define PREFABS_H

namespace IRECS {
    enum PrefabTypes {
        kGLFWJoystick,
        kPlayer,
        kVoxelSprite,
        kTestBlock,
        kSingleVoxel,
        kMidiDevice,
        kMidiMessageOut,
        kCamera,
        kVoxelParticle,
        kMidiSequenceAnimated
    };

    template<PrefabTypes type>
    struct Prefab;

    // struct Prefab {
    //     template<PrefabTypes type, typename... Args>
    //     static EntityHandle create(Args&&... args);
    // };

    // template<PrefabTypes type, typename... Args>
    // EntityHandle create(Args&&... args);
}

#endif /* PREFABS_H */
