#ifndef COMPONENT_ROTATION_TARGET_H
#define COMPONENT_ROTATION_TARGET_H

// C_RotationTarget — drives an entity's LOCAL rotation from a normalized
// control input. The rotation sibling of C_GotoEasing3D (which eases local
// *position*): same authoring/eval shape — a component plus a per-frame system
// that writes C_LocalTransform, with optional easing and no dirty flag.
//
// ROTATION_TARGET_LOCAL_TRANSFORM normalizes `input_` to [0,1] across
// [inputMin_, inputMax_], runs it through `easingFunction_` (a response curve,
// default linear), maps the result onto [minAngle_, maxAngle_], and writes
// C_LocalTransform.rotation_ as that angle about `axis_`. PROPAGATE_TRANSFORM
// then composes the result into C_WorldTransform.
//
// Unlike C_GotoEasing3D this is input-driven, not time-driven: there is no
// duration or `done_` completion. A creation updates `input_` each frame from
// any signal — a MIDI CC, a slider, a [0,1] sensor value — and the entity holds
// the last mapped angle on frames where the input does not change. Because the
// system writes the rotation absolutely (not accumulated), C_RotationTarget owns
// the entity's local rotation: don't pair it with C_AutoSpin on the same entity,
// just as C_GotoEasing3D owns local translation.
//
// `axis_` need not be normalized — IRMath::quatAxisAngle normalizes internally.
// A zero `axis_` is a no-op (the system early-returns), keeping the world
// transform free of the NaN quaternion a zero axis would otherwise yield.

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_RotationTarget {
    // Local axis the entity rotates about (need not be unit).
    IRMath::vec3 axis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
    // Output angle range, radians: minAngle_ at normalized input 0, maxAngle_
    // at normalized input 1.
    float minAngle_ = 0.0f;
    float maxAngle_ = 0.0f;
    // Input value range used to normalize `input_` into [0,1]. Defaults pass an
    // already-normalized [0,1] input straight through.
    float inputMin_ = 0.0f;
    float inputMax_ = 1.0f;
    // Live control value, updated per frame by the creation.
    float input_ = 0.0f;
    // Response curve applied to the normalized input before the angle map.
    GLMEasingFunction easingFunction_;

    C_RotationTarget(
        IRMath::vec3 axis,
        float minAngle,
        float maxAngle,
        float input = 0.0f,
        float inputMin = 0.0f,
        float inputMax = 1.0f,
        IREasingFunctions easingFunction = IREasingFunctions::kLinearInterpolation
    )
        : axis_{axis}
        , minAngle_{minAngle}
        , maxAngle_{maxAngle}
        , inputMin_{inputMin}
        , inputMax_{inputMax}
        , input_{input}
        , easingFunction_{kEasingFunctions.at(easingFunction)} {}

    C_RotationTarget()
        : C_RotationTarget{IRMath::vec3(0.0f, 0.0f, 1.0f), 0.0f, 0.0f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ROTATION_TARGET_H */
