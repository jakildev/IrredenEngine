#ifndef COMPONENT_AUTO_SPIN_H
#define COMPONENT_AUTO_SPIN_H

// C_AutoSpin — pair with C_LocalTransform to drive a constant per-frame
// rotation around `axis_`. SYSTEM_AUTO_SPIN_LOCAL_TRANSFORM accumulates the
// rotation into `C_LocalTransform.rotation_` each UPDATE tick, before
// PROPAGATE_TRANSFORM composes world transforms.
//
// `axis_` does not need to be normalized — the system passes it through
// IRMath::quatAxisAngle which normalizes internally. A zero `axis_` is a
// no-op (the system early-returns). Tied to UPDATE frequency, matching the
// existing per-frame AUTO_YAW_ROTATE convention; for demos this is the
// expected behavior.

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_AutoSpin {
    IRMath::vec3 axis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
    float radiansPerFrame_ = 0.0f;

    C_AutoSpin() = default;

    C_AutoSpin(IRMath::vec3 axis, float radiansPerFrame)
        : axis_{axis}
        , radiansPerFrame_{radiansPerFrame} {}
};

} // namespace IRComponents

#endif /* COMPONENT_AUTO_SPIN_H */
