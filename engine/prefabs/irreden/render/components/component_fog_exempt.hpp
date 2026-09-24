#ifndef COMPONENT_FOG_EXEMPT_H
#define COMPONENT_FOG_EXEMPT_H

namespace IRComponents {

// Fog subject class EXEMPT: never fogged. Empty tag: `FOG_SUBJECT_ADOPT`
// runs over `Exclude<C_FogExempt>`, and `FOG_SUBJECT_EXEMPT` pins the tagged
// voxel set's carrier at factor 255 so every raster route renders it whole.
// Attach at construction or through `IRPrefab::Fog::setSubjectClass`.
struct C_FogExempt {};

} // namespace IRComponents

#endif /* COMPONENT_FOG_EXEMPT_H */
