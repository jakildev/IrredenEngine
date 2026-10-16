#ifndef SDF_VOXEL_SCENE_H
#define SDF_VOXEL_SCENE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/sdf_bake.hpp>

namespace IRShapeDebug {

struct SdfVoxelScene {
    enum class Pose { REST, ROOT, PARENTED };

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

    static void initialize(bool baked, bool blockers, Pose pose = Pose::REST) {
        using namespace IRComponents;
        using namespace IRMath;
        struct Subject {
            SDF::ShapeType type_;
            vec4 params_;
            vec3 position_;
            Color color_;
        };
        const Subject subjects[] = {
            {SDF::ShapeType::SPHERE, vec4(4, 0, 0, 0), vec3(-9, -9, 0), Color{100, 180, 220, 255}},
            {SDF::ShapeType::CYLINDER, vec4(4, 0, 8, 0), vec3(9, -9, 0), Color{170, 110, 220, 255}},
            {SDF::ShapeType::CONE, vec4(4, 0, 8, 0), vec3(-9, 9, 0), Color{235, 175, 100, 255}},
            {SDF::ShapeType::TORUS, vec4(3, 1.5f, 0, 0), vec3(9, 9, 0), Color{130, 210, 145, 255}},
        };
        std::size_t allocated = 0;
        std::size_t occupied = 0;
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
        for (const auto &subject : subjects) {
            C_LocalTransform transform{subject.position_};
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
                const auto dense = IRPrefab::Voxel::bakeSdfGrid(
                    ivec3(11),
                    subject.type_,
                    subject.params_,
                    subject.color_
                );
                allocated += dense.voxels_.size();
                for (const auto &voxel : dense.voxels_) {
                    occupied += voxel.color_.alpha_ != 0;
                }
                entity = IREntity::createEntity(
                    transform,
                    IRPrefab::DenseVoxel::toComponent(dense, EntityAnchor::CENTER)
                );
            } else {
                entity = IREntity::createEntity(
                    transform,
                    C_ShapeDescriptor{subject.type_, subject.params_, subject.color_}
                );
            }
            if (pose == Pose::PARENTED) {
                IREntity::setParent(entity, parent);
            }
            if (blockers) {
                IREntity::createEntity(
                    C_LocalTransform{subject.position_ + vec3(3.5f, 8.5f, -8.0f)},
                    C_VoxelSetNew{ivec3(3, 2, 1), Color{210, 205, 195, 255}, EntityAnchor::CENTER}
                );
            }
        }
        IREntity::createEntity(
            C_LocalTransform{vec3(0, 0, 6)},
            C_VoxelSetNew{ivec3(48, 48, 1), Color{130, 135, 145, 255}, EntityAnchor::CENTER},
            C_LightBlocker{false, false, 0.0f}
        );
        IREntity::createEntity(
            C_LocalTransform{vec3(0, 0, -3)},
            C_LightSource{
                LightType::EMISSIVE,
                Color{80, 200, 255, 255},
                1.5f,
                static_cast<std::uint8_t>(24)
            }
        );
        IR_LOG_INFO(
            "SDF voxel scene: representation={} blockers={} pose={} subjects=4 "
            "subject_allocated_slots={} subject_occupied_cells={} subject_record_bytes={}",
            baked ? "baked" : "direct",
            blockers,
            static_cast<int>(pose),
            allocated,
            occupied,
            allocated * sizeof(C_Voxel)
        );
    }
};

} // namespace IRShapeDebug

#endif /* SDF_VOXEL_SCENE_H */
