#ifndef SYSTEM_COLLISION_RESPONSE_H
#define SYSTEM_COLLISION_RESPONSE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_collision_response.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Rounding allowance, in float ulps of the pair's coordinate magnitude, used
// only to classify contacts. The sweep start, two separately built faces and
// the center-space subtractions each lose up to a couple of ulps; 8 doubles
// that worst case.
constexpr float kCollisionContactTolUlps = 8.0f;

namespace detail {

// A solid collider's world box for one tick. `magnitude_` is the largest
// |component| of its translation plus those of its centerOffset_ and
// halfExtents_, the scale its faces were rounded at.
struct CollisionSolid {
    vec3 min_;
    vec3 max_;
    std::uint32_t layer_;
    float magnitude_;
};

// One solid in the body's center space: the body's translation overlaps the
// solid exactly when it lies inside (eMin_, eMax_) on every axis.
struct CollisionCandidate {
    vec3 eMin_;
    vec3 eMax_;
    float tol_;
};

inline float maxAbsComponent(vec3 value) {
    const vec3 magnitude = IRMath::abs(value);
    return IRMath::max(magnitude.x, IRMath::max(magnitude.y, magnitude.z));
}

inline float collisionContactTolerance(float magnitude, float tolUlps) {
    return tolUlps * std::numeric_limits<float>::epsilon() * IRMath::max(1.0f, magnitude);
}

inline CollisionSolid
makeCollisionSolid(vec3 translation, const C_ColliderIso3DAABB &collider, std::uint32_t layer) {
    const vec3 center = translation + collider.centerOffset_;
    return CollisionSolid{
        center - collider.halfExtents_,
        center + collider.halfExtents_,
        layer,
        maxAbsComponent(translation) + maxAbsComponent(collider.centerOffset_) +
            maxAbsComponent(collider.halfExtents_)
    };
}

// Open interval shrunk by `tol`: touching, or overlapping by less than the
// rounding allowance, is not overlapping.
inline bool overlapsOpenWithin(float value, float eMin, float eMax, float tol) {
    return value > eMin + tol && value < eMax - tol;
}

// The contact response on axis `a`. `intoSign` is the sign of motion into
// the face that stopped the body.
inline void
applyCollisionContact(vec3 &velocity, C_CollisionResponse &response, int a, float intoSign) {
    const float approachSpeed = velocity[a] * intoSign;
    if (approachSpeed <= 0.0f) {
        return;
    }
    const int b = (a + 1) % 3;
    const int c = (a + 2) % 3;
    const float keep = 1.0f - response.friction_;
    float tangentB = velocity[b] * keep;
    float tangentC = velocity[c] * keep;
    // Up is -z, so a support face is one met while moving +z.
    const bool support = a == 2 && intoSign > 0.0f;
    if (response.restOnSupport_ && support && approachSpeed < response.restSpeed_) {
        velocity[a] = 0.0f;
        if (IRMath::length(vec2(tangentB, tangentC)) < response.restSpeed_) {
            tangentB = 0.0f;
            tangentC = 0.0f;
        }
        response.resting_ = true;
    } else {
        velocity[a] = -response.restitution_ * velocity[a];
    }
    velocity[b] = tangentB;
    velocity[c] = tangentC;
}

// Resolves one body against `solids` and its floor plane for one tick.
//
// `translation` is the body's integrated world translation on entry and its
// resolved one on exit; `velocity` is the velocity that produced it. The
// sweep starts at `translation - velocity * dt`, the previous resolved
// position up to rounding, so no velocity writer may run between the
// integration and this call.
//
// Axes resolve in the order z, x, y. On each, the body moves to its
// integrated coordinate unless it meets the nearest solid face it overlaps on
// the other two axes, where it stops exactly on the face. Overlap uses open
// intervals widened by the contact tolerance, which only decides which faces
// count: the stop is the face itself, never an offset from it, so a resting
// body's translation is bit-identical from tick to tick. A solid the body
// already overlaps at the sweep start is ignored for the tick. `scratch` is
// cleared on entry.
inline void resolveCollisionBody(
    vec3 &translation,
    vec3 &velocity,
    C_CollisionResponse &response,
    const C_ColliderIso3DAABB &collider,
    const C_CollisionLayer &layer,
    std::span<const CollisionSolid> solids,
    float dt,
    float tolUlps,
    std::vector<CollisionCandidate> &scratch
) {
    scratch.clear();
    response.resting_ = false;

    const vec3 end = translation;
    const vec3 start = end - velocity * vec3(dt);
    const vec3 nearOffset = collider.centerOffset_ + collider.halfExtents_;
    const vec3 farOffset = collider.centerOffset_ - collider.halfExtents_;
    const float floorTarget = response.floorZ_ - nearOffset.z;

    vec3 sweptMin = IRMath::min(start, end);
    const vec3 sweptMax = IRMath::max(start, end);
    if (response.hasFloor_) {
        sweptMin.z = IRMath::min(sweptMin.z, floorTarget);
    }
    // The body's box over the whole sweep, in world space.
    const vec3 sweptBoxMin = sweptMin + farOffset;
    const vec3 sweptBoxMax = sweptMax + nearOffset;

    if (layer.isSolid_) {
        const float bodyMagnitude = IRMath::max(maxAbsComponent(start), maxAbsComponent(end)) +
                                    maxAbsComponent(collider.centerOffset_) +
                                    maxAbsComponent(collider.halfExtents_);
        for (const CollisionSolid &solid : solids) {
            if ((layer.collidesWithMask_ & solid.layer_) == 0u) {
                continue;
            }
            const float tol =
                collisionContactTolerance(IRMath::max(bodyMagnitude, solid.magnitude_), tolUlps);
            // A clamp can leave the body up to `tol` outside its swept
            // bounds, and this world-space test rounds differently from the
            // center-space faces by under `tol`: pad 3 * tol so the reject
            // never drops a face the axis passes would admit.
            const float pad = 3.0f * tol;
            if (sweptBoxMax.x + pad < solid.min_.x || sweptBoxMin.x - pad > solid.max_.x ||
                sweptBoxMax.y + pad < solid.min_.y || sweptBoxMin.y - pad > solid.max_.y ||
                sweptBoxMax.z + pad < solid.min_.z || sweptBoxMin.z - pad > solid.max_.z) {
                continue;
            }
            const vec3 eMin = solid.min_ - nearOffset;
            const vec3 eMax = solid.max_ - farOffset;
            if (overlapsOpenWithin(start.x, eMin.x, eMax.x, tol) &&
                overlapsOpenWithin(start.y, eMin.y, eMax.y, tol) &&
                overlapsOpenWithin(start.z, eMin.z, eMax.z, tol)) {
                continue;
            }
            scratch.push_back(CollisionCandidate{eMin, eMax, tol});
        }
    }

    constexpr int kAxisOrder[3] = {2, 0, 1};
    vec3 position = start;
    for (const int a : kAxisOrder) {
        const int b = (a + 1) % 3;
        const int c = (a + 2) % 3;
        float target = end[a];
        float intoSign = 0.0f;
        if (end[a] != start[a]) {
            const bool movingPositive = end[a] > start[a];
            for (const CollisionCandidate &candidate : scratch) {
                if (!overlapsOpenWithin(
                        position[b],
                        candidate.eMin_[b],
                        candidate.eMax_[b],
                        candidate.tol_
                    ) ||
                    !overlapsOpenWithin(
                        position[c],
                        candidate.eMin_[c],
                        candidate.eMax_[c],
                        candidate.tol_
                    )) {
                    continue;
                }
                if (movingPositive) {
                    const float face = candidate.eMin_[a];
                    if (face >= position[a] - candidate.tol_ && face <= target) {
                        target = face;
                        intoSign = 1.0f;
                    }
                } else {
                    const float face = candidate.eMax_[a];
                    if (face <= position[a] + candidate.tol_ && face >= target) {
                        target = face;
                        intoSign = -1.0f;
                    }
                }
            }
        }
        if (a == 2 && response.hasFloor_ && target > floorTarget) {
            target = floorTarget;
            intoSign = 1.0f;
        }
        if (intoSign != 0.0f) {
            applyCollisionContact(velocity, response, a, intoSign);
        }
        position[a] = target;
    }
    translation = position;
}

} // namespace detail

// Bounces, slides and rests moving bodies on solid AABB colliders and on each
// body's optional floor plane.
//
// A body carries C_LocalTransform, C_WorldTransform, C_Velocity3D,
// C_ColliderIso3DAABB, C_CollisionLayer and C_CollisionResponse, and is a
// root entity: its velocity and translation delta are taken as world-space.
// A solid is any entity with C_WorldTransform, C_ColliderIso3DAABB and a solid
// C_CollisionLayer that does not carry C_CollisionResponse, taken at its
// current-tick position. A solid body responds to a solid when its own
// collidesWithMask_ includes the solid's layer; the solid's mask is never
// read. A non-solid body responds only to its floor.
//
// Place after VELOCITY_3D and PROPAGATE_TRANSFORM (and after
// COLLISION_NOTE_PLATFORM when registered), with no C_Velocity3D writer in
// between: the sweep start is recovered from this tick's velocity.
template <> struct System<COLLISION_RESPONSE> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    float contactTolUlps_ = kCollisionContactTolUlps;
    float deltaTime_ = 0.0f;
    std::vector<detail::CollisionSolid> solids_;
    // Candidate scratch per executor, indexed by IRJob::workerId() (0 = main
    // thread). A body's candidates are a subset of solids_, so beginTick
    // reserving solids_.size() in every slot keeps the tick allocation-free.
    std::vector<std::vector<detail::CollisionCandidate>> candidatesByWorker_;

    void beginTick() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);
        deltaTime_ = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
        solids_.clear();
        const auto nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<C_WorldTransform, C_ColliderIso3DAABB, C_CollisionLayer>(),
            IREntity::getArchetype<C_CollisionResponse>()
        );
        for (auto *node : nodes) {
            const auto &transforms = IREntity::getComponentData<C_WorldTransform>(node);
            const auto &colliders = IREntity::getComponentData<C_ColliderIso3DAABB>(node);
            const auto &layers = IREntity::getComponentData<C_CollisionLayer>(node);
            for (int i = 0; i < node->length_; ++i) {
                if (!layers[i].isSolid_) {
                    continue;
                }
                solids_.push_back(
                    detail::makeCollisionSolid(
                        transforms[i].translation_,
                        colliders[i],
                        layers[i].layer_
                    )
                );
            }
        }
        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        if (candidatesByWorker_.size() < slots) {
            candidatesByWorker_.resize(slots);
        }
        for (std::vector<detail::CollisionCandidate> &candidates : candidatesByWorker_) {
            candidates.reserve(solids_.size());
        }
    }

    void tick(
        C_LocalTransform &localTransform,
        C_WorldTransform &worldTransform,
        C_Velocity3D &velocity,
        const C_ColliderIso3DAABB &collider,
        const C_CollisionLayer &layer,
        C_CollisionResponse &response
    ) {
        std::vector<detail::CollisionCandidate> &candidates =
            candidatesByWorker_[static_cast<std::size_t>(IRJob::workerId())];
        const vec3 integrated = worldTransform.translation_;
        detail::resolveCollisionBody(
            worldTransform.translation_,
            velocity.velocity_,
            response,
            collider,
            layer,
            solids_,
            deltaTime_,
            contactTolUlps_,
            candidates
        );
        localTransform.translation_ += worldTransform.translation_ - integrated;
    }

    std::size_t solidCount() const {
        return solids_.size();
    }

    static SystemId create() {
        return registerSystem<
            COLLISION_RESPONSE,
            C_LocalTransform,
            C_WorldTransform,
            C_Velocity3D,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_CollisionResponse>("CollisionResponse");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COLLISION_RESPONSE_H */
