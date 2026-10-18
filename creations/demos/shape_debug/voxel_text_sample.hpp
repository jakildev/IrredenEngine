#ifndef VOXEL_TEXT_SAMPLE_H
#define VOXEL_TEXT_SAMPLE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/voxel_text.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <string_view>
#include <vector>

namespace IRShapeDebug {

// The --voxel-text-sample scene: three voxel titles standing on a lit floor,
// built facing the camera yaw the run captures at. One title runs along the
// default path (world +Y at yaw 0), one heads world -X, and one bends from
// 160 to 200 degrees through a polyline. Every heading of the last two lies
// strictly between 135 and 225 degrees, where each segment advances screen-
// right at yaws 0, 45 and 90, so all three read left to right in every
// capture. The origins keep the titles' projected boxes at least 16 iso units
// apart at those yaws (at yaw 90 the default path runs parallel to the -X
// title).
struct VoxelTextSample {
    static constexpr float kZoom = 2.0f;

    struct Title {
        std::string_view text_;
        IRPrefab::VoxelText::VoxelTextOptions options_;
    };

    static std::vector<Title> titles() {
        using IRMath::vec3;
        constexpr vec3 kBentOrigin = vec3(40.0f, -56.0f, 0.0f);
        const auto stepAt = [](float degrees, float length) {
            const float heading = degrees * (IRMath::kPi / 180.0f);
            return vec3(IRMath::cos(heading), IRMath::sin(heading), 0.0f) * length;
        };
        const vec3 bend = kBentOrigin + stepAt(160.0f, 30.0f);

        std::vector<Title> result(3);
        result[0].text_ = "IRREDEN";
        result[0].options_.origin_ = vec3(-16.0f, 0.0f, 0.0f);
        result[0].options_.color_ = IRMath::Color{235, 200, 90, 255};

        result[1].text_ = "VOXEL";
        result[1].options_.origin_ = vec3(72.0f, 8.0f, 0.0f);
        result[1].options_.color_ = IRMath::Color{110, 200, 235, 255};
        result[1].options_.path_.headings_ = {IRMath::kPi};

        result[2].text_ = "BENDING";
        result[2].options_.origin_ = kBentOrigin;
        result[2].options_.color_ = IRMath::Color{225, 120, 150, 255};
        result[2].options_.path_.points_ = {bend, bend + stepAt(200.0f, 30.0f)};
        return result;
    }

    // Builds the floor and the titles, the titles facing `yawRadians`.
    static void initialize(float yawRadians) {
        IRPrefab::Camera::setYaw(yawRadians);
        IREntity::createEntity(
            IRComponents::C_LocalTransform{IRMath::vec3(-112.0f, -80.0f, 1.0f)},
            IRComponents::C_VoxelSetNew{
                IRMath::ivec3(208, 176, 1),
                IRMath::Color{120, 125, 135, 255},
                IRComponents::EntityAnchor::CORNER
            }
        );
        for (const Title &title : titles()) {
            const IREntity::EntityId root =
                IRPrefab::VoxelText::create(title.text_, title.options_);
            IR_LOG_INFO(
                "[voxel-text] '{}' letters={}",
                title.text_,
                IREntity::getEntityManager().getChildren(root).size()
            );
        }
    }
};

} // namespace IRShapeDebug

#endif /* VOXEL_TEXT_SAMPLE_H */
