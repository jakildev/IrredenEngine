#ifndef COMPONENT_LIGHT_BLOCKER_H
#define COMPONENT_LIGHT_BLOCKER_H

namespace IRComponents {

/// Marks an entity as an obstacle for lighting computations.
/// Voxels and shapes are implicit blockers by default; this component
/// lets Lua scripts opt geometry in or out of specific lighting passes.
struct C_LightBlocker {
    /// If true, solid voxels from this entity block LOS ray casting
    /// (fog-of-war and shadow phases). Set false for decorative geometry
    /// that should not occlude sight lines (e.g. foliage particles).
    bool  blocksLOS_;
    /// If true, this entity participates in the shadow height-map sweep.
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
