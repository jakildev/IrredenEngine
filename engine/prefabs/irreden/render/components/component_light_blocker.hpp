#ifndef COMPONENT_LIGHT_BLOCKER_H
#define COMPONENT_LIGHT_BLOCKER_H

namespace IRComponents {

/// Supplies SDF-shape fog and light-occlusion options.
struct C_LightBlocker {
    /// Opts an SDF shape into fog LOS and point/spot-light occlusion.
    /// Voxel-set fog occlusion ignores this field: non-transparent voxels
    /// occlude unless their BODY fog subject class sets kFogWholeBodyExempt.
    /// Neither voxel sets nor SDF shapes use this field for sun shadows.
    bool  blocksLOS_;
    /// Reserved for a future shadow consumer; no current fog, light-grid,
    /// or sun-shadow pass reads this field.
    bool  castsShadow_;
    /// How opaque this entity is to flood-fill light, in [0.0, 1.0].
    /// 0.0 = fully transparent (light passes through); 1.0 = fully solid.
    float opacity_;

    C_LightBlocker(bool blocksLOS, bool castsShadow, float opacity)
        : blocksLOS_{blocksLOS}
        , castsShadow_{castsShadow}
        , opacity_{opacity}
    {}

    C_LightBlocker()
        : C_LightBlocker{true, true, 1.0f}
    {}
};

} // namespace IRComponents

#endif /* COMPONENT_LIGHT_BLOCKER_H */
