#ifndef COMPONENT_GLITCH_SUBJECT_H
#define COMPONENT_GLITCH_SUBJECT_H

namespace IRComponents {

// Marks an entity whose main-canvas trixels `TRIXEL_GLITCH` glitches. Plain
// data: validation and the Lua surface live in `IRPrefab::Glitch`
// (`glitch_subject.hpp`), the only writer that checks the invariants below.
// The id the canvas carries is the entity that stamped the trixel (the voxel
// set or the shape), so a multi-set object marks each set.
//
// Invariants a stored value satisfies: every weight finite and in [0, 1];
// `spill_` in [0, kSubjectMaxSpill]; `depthNear_ <= depthFar_` when
// `depthEnabled_`.
struct C_GlitchSubject {
    // Scales every effect weight; 0 parks the subject.
    float weight_ = 1.0f;
    float tearWeight_ = 0.0f;
    float holdWeight_ = 0.0f;
    float splitWeight_ = 0.0f;
    float echoWeight_ = 0.0f;
    float dissolveWeight_ = 0.0f;
    // Chebyshev radius in canvas trixels around the subject's silhouette that
    // its glitch reaches; two subjects interfere where their bands overlap.
    int spill_ = 0;
    // Inclusive decoded single-canvas iso-depth band (`decodeDepthSingle`)
    // that confines tear and hold. Smaller is nearer.
    bool depthEnabled_ = false;
    int depthNear_ = 0;
    int depthFar_ = 0;
};

} // namespace IRComponents

#endif /* COMPONENT_GLITCH_SUBJECT_H */
