#ifndef COMPONENT_FOG_EXEMPT_H
#define COMPONENT_FOG_EXEMPT_H

namespace IRComponents {

// Fog subject class EXEMPT: never fogged. Adoption excludes this marker; the
// matching realization system pins each raster kind's BODY carrier at factor
// 255. Attach at construction or through `IRPrefab::Fog::setSubjectClass`.
struct C_FogExempt {};

} // namespace IRComponents

#endif /* COMPONENT_FOG_EXEMPT_H */
