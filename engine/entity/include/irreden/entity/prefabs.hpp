#ifndef PREFABS_H
#define PREFABS_H

namespace IREntity {

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
    kVoxelPoolCanvas,
    kTrixelCanvas,
    kFramebuffer
};

template <PrefabTypes type> struct Prefab;

// struct Prefab {
//     template<PrefabTypes type, typename... Args>
//     static EntityHandle create(Args&&... args);
// };

// template<PrefabTypes type, typename... Args>
// EntityHandle create(Args&&... args);
} // namespace IREntity

#endif /* PREFABS_H */
