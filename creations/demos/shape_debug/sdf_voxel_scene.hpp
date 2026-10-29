#ifndef SDF_VOXEL_SCENE_H
#define SDF_VOXEL_SCENE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/sdf_bake.hpp>

#include <array>
#include <chrono>
#include <cstdio>

namespace IRShapeDebug {

struct SdfVoxelScene {
    enum class Pose { REST, ROOT, PARENTED };
    enum class Presentation { GRID, DETACHED_SOURCE, DETACHED_REVOXELIZED };

    static constexpr IRVideo::AutoScreenshotShot shots_[] = {
        {4.0f, IRMath::vec2(0), 0.0f, "sdf_voxel_yaw0"},
        {4.0f, IRMath::vec2(0), IRMath::kQuarterPi, "sdf_voxel_yaw45"},
        {4.0f, IRMath::vec2(0), IRMath::kHalfPi, "sdf_voxel_yaw90"},
        {4.0f, IRMath::vec2(0), IRMath::kPi, "sdf_voxel_yaw180"},
        {4.0f, IRMath::vec2(0), IRMath::kPi * 1.5f, "sdf_voxel_yaw270"},
        {2.0f, IRMath::vec2(0), IRMath::kQuarterPi, "sdf_voxel_zoom2"},
    };

    static constexpr IRVideo::AutoScreenshotShot posedShots_[] = {
        {4.0f, IRMath::vec2(0), 0.0f, "sdf_voxel_pose_yaw0"},
        {4.0f, IRMath::vec2(0), IRMath::kQuarterPi, "sdf_voxel_pose_yaw45"},
        {4.0f, IRMath::vec2(0), IRMath::kHalfPi, "sdf_voxel_pose_yaw90"},
        {4.0f, IRMath::vec2(0), IRMath::kPi, "sdf_voxel_pose_yaw180"},
        {4.0f, IRMath::vec2(0), IRMath::kPi * 1.5f, "sdf_voxel_pose_yaw270"},
        {2.0f, IRMath::vec2(0), IRMath::kQuarterPi, "sdf_voxel_pose_zoom2"},
    };

    static void initialize(
        bool baked,
        bool blockers,
        Pose pose = Pose::REST,
        bool localLights = true,
        Presentation presentation = Presentation::GRID,
        int density = 1,
        int copies = 1,
        bool overlap = false,
        bool moving = false
    ) {
        using namespace IRComponents;
        using namespace IRMath;
        struct Subject {
            SDF::ShapeType type_;
            vec4 params_;
            vec3 position_;
            Color color_;
        };
        const std::array<Subject, 4> subjects{{
            {SDF::ShapeType::SPHERE, vec4(4, 0, 0, 0), vec3(-9, -9, 0), Color{100, 180, 220, 255}},
            {SDF::ShapeType::CYLINDER, vec4(4, 0, 8, 0), vec3(9, -9, 0), Color{170, 110, 220, 255}},
            {SDF::ShapeType::CONE, vec4(4, 0, 8, 0), vec3(-9, 9, 0), Color{235, 175, 100, 255}},
            {SDF::ShapeType::TORUS, vec4(3, 1.5f, 0, 0), vec3(9, 9, 0), Color{130, 210, 145, 255}},
        }};
        std::size_t allocated = 0;
        std::size_t occupied = 0;
        std::chrono::nanoseconds bakeTime{};
        std::array<IRAsset::DenseVoxelSet, 4> denseSubjects;
        std::array<std::size_t, 4> occupiedPerSubject{};
        if (baked) {
            const int edge = 10 * density + 1;
            for (std::size_t index = 0; index < subjects.size(); ++index) {
                const auto started = std::chrono::steady_clock::now();
                denseSubjects[index] = IRPrefab::Voxel::bakeSdfGrid(
                    ivec3(edge),
                    subjects[index].type_,
                    subjects[index].params_ * static_cast<float>(density),
                    subjects[index].color_
                );
                bakeTime += std::chrono::steady_clock::now() - started;
                for (const auto &voxel : denseSubjects[index].voxels_) {
                    occupiedPerSubject[index] += voxel.color_.alpha_ != 0;
                }
            }
        }
        const vec3 parentPosition(2.5f, -3.5f, -1.5f);
        const vec4 localRotation = quatAxisAngle(normalize(vec3(1, 2, 0.5f)), kPi / 5.0f);
        // Independent half-turn composition: Rz(pi) * (x,y,z,w) = (-y,x,w,-z).
        // The root arm must not use the propagation helper it is checking.
        const vec4
            worldRotation(-localRotation.y, localRotation.x, localRotation.w, -localRotation.z);
        const auto parent =
            pose == Pose::REST
                ? IREntity::kNullEntity
                : IREntity::createEntity(C_LocalTransform{parentPosition, vec4(0, 0, 1, 0)});
        if (moving && pose == Pose::PARENTED) {
            IREntity::setComponent(parent, C_AutoSpin{vec3(0.5f, 1.0f, 1.0f), 0.004f});
        }
        for (int copy = 0; copy < copies; ++copy) {
            const int column = copy % 4;
            const int row = copy / 4;
            const vec3 copyOffset = overlap ? vec3(copy * 0.4f, copy * -0.3f, copy * 0.2f)
                                            : vec3(column * 44.0f, row * 44.0f, 0.0f);
            for (std::size_t subjectIndex = 0; subjectIndex < subjects.size(); ++subjectIndex) {
                const auto &subject = subjects[subjectIndex];
                C_LocalTransform transform{subject.position_ + copyOffset};
                if (pose != Pose::REST) {
                    transform.translation_ += vec3(0.25f, -0.5f, -2.0f);
                    transform.rotation_ = worldRotation;
                }
                if (pose == Pose::PARENTED) {
                    const vec3 relative = transform.translation_ - parentPosition;
                    transform.translation_ = vec3(-relative.x, -relative.y, relative.z);
                    transform.rotation_ = localRotation;
                }
                IREntity::EntityId entity;
                if (baked) {
                    const auto &dense = denseSubjects[subjectIndex];
                    allocated += dense.voxels_.size();
                    occupied += occupiedPerSubject[subjectIndex];
                    transform.scale_ = vec3(1.0f / static_cast<float>(density));
                    entity = IREntity::createEntity(
                        transform,
                        IRPrefab::DenseVoxel::toComponent(dense, EntityAnchor::CENTER)
                    );
                    if (presentation != Presentation::GRID) {
                        IRPrefab::RotationMode::setMode(
                            entity,
                            presentation == Presentation::DETACHED_SOURCE
                                ? RotationMode::DETACHED
                                : RotationMode::DETACHED_REVOXELIZE
                        );
                    }
                } else {
                    entity = IREntity::createEntity(
                        transform,
                        C_ShapeDescriptor{subject.type_, subject.params_, subject.color_}
                    );
                }
                if (pose == Pose::PARENTED) {
                    IREntity::setParent(entity, parent);
                } else if (moving) {
                    IREntity::setComponent(entity, C_AutoSpin{vec3(0.5f, 1.0f, 1.0f), 0.004f});
                }
                if (blockers) {
                    IREntity::createEntity(
                        C_LocalTransform{subject.position_ + copyOffset + vec3(3.5f, 8.5f, -8.0f)},
                        C_VoxelSetNew{
                            ivec3(3, 2, 1),
                            Color{210, 205, 195, 255},
                            EntityAnchor::CENTER
                        }
                    );
                }
            }
        }
        const int floorEdge = copies == 1 ? 48 : 192;
        IREntity::createEntity(
            C_LocalTransform{vec3(0, 0, 6)},
            C_VoxelSetNew{
                ivec3(floorEdge, floorEdge, 1),
                Color{130, 135, 145, 255},
                EntityAnchor::CENTER
            },
            C_LightBlocker{false, false, 0.0f}
        );
        if (localLights) {
            IREntity::createEntity(
                C_LocalTransform{vec3(0, 0, -3)},
                C_LightSource{
                    LightType::EMISSIVE,
                    Color{80, 200, 255, 255},
                    1.5f,
                    static_cast<std::uint8_t>(24)
                }
            );
        }
        IR_LOG_INFO(
            "SDF voxel scene: representation={} blockers={} pose={} local_lights={} subjects={} "
            "subject_allocated_slots={} subject_occupied_cells={} subject_record_bytes={}",
            baked ? "baked" : "direct",
            blockers,
            static_cast<int>(pose),
            localLights ? 1 : 0,
            copies * 4,
            allocated,
            occupied,
            allocated * sizeof(C_Voxel)
        );
        std::printf(
            "SDF-OCCUPANCY representation=%s presentation=%d density=%d copies=%d overlap=%d "
            "moving=%d bake_us=%lld allocated_slots=%zu occupied_cells=%zu record_bytes=%zu\n",
            baked ? "baked" : "direct",
            static_cast<int>(presentation),
            density,
            copies,
            overlap ? 1 : 0,
            moving ? 1 : 0,
            static_cast<long long>(
                std::chrono::duration_cast<std::chrono::microseconds>(bakeTime).count()
            ),
            allocated,
            occupied,
            allocated * sizeof(C_Voxel)
        );
        std::fflush(stdout);
    }
};

} // namespace IRShapeDebug

#endif /* SDF_VOXEL_SCENE_H */
