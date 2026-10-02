#ifndef COMPONENT_FOG_FIELD_H
#define COMPONENT_FOG_FIELD_H

namespace IRComponents {

// Fog subject class FIELD: terrain-tier matter the fog pass evaluates per
// sample and paints, never as one body. Empty tag: `FOG_SUBJECT_ADOPT` runs
// over `Exclude<C_FogField>`, so a tagged voxel set is never adopted as a
// BODY. Attach at construction or through `IRPrefab::Fog::setSubjectClass`.
struct C_FogField {};

} // namespace IRComponents

#endif /* COMPONENT_FOG_FIELD_H */
