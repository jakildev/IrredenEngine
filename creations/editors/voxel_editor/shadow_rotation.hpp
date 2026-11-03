#ifndef SHADOW_ROTATION_H
#define SHADOW_ROTATION_H

#include "session_builder.hpp"
#include "authored_voxel_fingerprint.hpp"
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/render/per_axis_surface_lighting.hpp>

#include <array>
#include <cstdint>

namespace IRVoxelEditor {

inline constexpr std::array<float, 20> kShadowRotationYaws{0.0f,   0.25f,  30.0f,  44.75f, 45.0f,
                                                           45.25f, 67.0f,  89.75f, 90.0f,  90.25f,
                                                           90.0f,  89.75f, 67.0f,  45.25f, 45.0f,
                                                           44.75f, 30.0f,  0.25f,  0.0f,   0.0f};
inline constexpr int kShadowRotationShotCount = static_cast<int>(kShadowRotationYaws.size()) * 2;

inline float shadowRotationYawDegrees(int shot) {
    return kShadowRotationYaws[static_cast<std::size_t>(shot / 2)];
}
inline bool shadowRotationShadowsEnabled(int shot) {
    return shot % 2 == 0;
}

// Fixed authored cells; derived visibility flags are excluded from the fingerprint.
inline bool seedShadowRotationScene(IRComponents::C_VoxelSetNew &set) {
    if (set.size_.x < 16 || set.size_.y < 16 || set.size_.z < 16)
        return false;
    set.deactivateAll();
    auto records = set.mutableAuthoredRecords();
    const auto put = [&](int x, int y, int z, IRMath::Color color) {
        records[IRMath::index3DtoIndex1D(IRMath::ivec3(x, y, z), set.size_)] =
            IRComponents::C_Voxel{color};
    };
    const int floor = set.size_.z - 1;
    for (int y = 2; y < 14; ++y)
        for (int x = 2; x < 14; ++x)
            put(x, y, floor, {200, 200, 200, 255});
    for (int y = 5; y < 8; ++y)
        for (int x = 5; x < 8; ++x)
            for (int height = 1; height <= 5; ++height)
                put(x, y, floor - height, {240, 150, 70, 255});
    for (int x = 9; x < 13; ++x)
        for (int y = 6; y < 9; ++y)
            for (int height = 1; height <= x - 8; ++height)
                put(x, y, floor - height, {80, 170, 230, 255});
    set.resyncAfterAuthoredEdits();
    return true;
}

namespace Session {
bool evaluateShadowRotationCheck(const void *context, std::string &actual);

inline Recipe buildShadowRotation() {
    Recipe recipe;
    recipe.name_ = "shadow_rotation";
    static constexpr auto checks = [] {
        std::array<int, kShadowRotationShotCount> values{};
        for (int i = 0; i < kShadowRotationShotCount; ++i)
            values[static_cast<std::size_t>(i)] = i;
        return values;
    }();
    for (const int shot : checks) {
        Segment segment;
        segment.label_ = "shadow_rotation_pose" + std::to_string(shot / 2) +
                         (shadowRotationShadowsEnabled(shot) ? "_on" : "_off");
        segment.zoom_ = 8.0f;
        IRVideo::GuiInputEvent move{};
        move.type_ = IRVideo::GuiInputEvent::Type::MOVE;
        move.frameOffset_ = 12;
        move.screenPx_ = IRMath::ivec2(0);
        segment.events_.push_back(move);
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateShadowRotationCheck,
                &checks[static_cast<std::size_t>(shot)],
                "fixed_geometry_and_shadow_pose"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}
} // namespace Session
} // namespace IRVoxelEditor

#endif /* SHADOW_ROTATION_H */
