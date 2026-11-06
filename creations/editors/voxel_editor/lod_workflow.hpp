#ifndef IR_VOXEL_EDITOR_LOD_WORKFLOW_H
#define IR_VOXEL_EDITOR_LOD_WORKFLOW_H

#include "authored_voxel_fingerprint.hpp"
#include "lod_comparison_panel.hpp"
#include "local_part_edit.hpp"
#include "session_builder.hpp"

#include <array>
#include <sstream>

namespace IRVoxelEditor {

inline constexpr int kLodWorkflowShotCount = 9;

inline IREntity::EntityId prepareLodWorkflow(EntityScene &scene) {
    scene.begin();
    const auto fine = scene.addVoxelPart(
        {6, 4, 3},
        IRMath::vec3(0),
        IRComponents::EntityAnchor::CENTER,
        {224, 64, 32, 255}
    );
    scene.addVoxelPart(
        {3, 2, 2},
        IRMath::vec3(0),
        IRComponents::EntityAnchor::CENTER,
        {32, 96, 224, 255}
    );
    scene.setPartBand(0, IRRender::LodLevel::LOD_0, IRRender::LodLevel::LOD_0);
    scene.setPartBand(1, IRRender::LodLevel::LOD_4, IRRender::LodLevel::LOD_4);
    scene.select(0, false);
    return fine;
}

struct LodWorkflowProbe {
    std::array<AuthoredVoxelFingerprint, 2> fingerprints_{};
    std::array<IRComponents::C_LocalTransform, 2> poses_{};
    std::array<IREntity::EntityId, 2> entities_{};
    EditorStageView view_;
    bool baseline_ = false;
    std::array<std::uint64_t, 5> disturbedCaptureHashes_{};

    bool evaluate(
        int shot,
        const EntityScene &scene,
        const LodCapturePlan &captures,
        const LodComparisonPanel &panel,
        bool active,
        std::string &actual
    ) {
        if (shot == 0) {
            if (scene.parts().size() != 2) {
                actual = "fixture requires two authored variants";
                return false;
            }
            for (std::size_t i = 0; i < 2; ++i) {
                entities_[i] = scene.parts()[i].entity_;
                fingerprints_[i] = authoredVoxelFingerprint(
                    IREntity::getComponent<IRComponents::C_VoxelSetNew>(entities_[i])
                );
                poses_[i] = IREntity::getComponent<IRComponents::C_LocalTransform>(entities_[i]);
            }
            view_.capture();
            baseline_ = true;
        }
        bool unchanged = baseline_ && scene.parts().size() >= 2;
        for (std::size_t i = 0; i < 2 && unchanged; ++i) {
            const auto &part = scene.parts()[i];
            if (part.entity_ != entities_[i]) {
                unchanged = false;
                break;
            }
            const auto &pose = IREntity::getComponent<IRComponents::C_LocalTransform>(part.entity_);
            unchanged = authoredVoxelFingerprint(
                            IREntity::getComponent<IRComponents::C_VoxelSetNew>(part.entity_)
                        ) == fingerprints_[i] &&
                        pose.translation_ == poses_[i].translation_ &&
                        pose.rotation_ == poses_[i].rotation_ && pose.scale_ == poses_[i].scale_ &&
                        part.parentId_.empty() &&
                        part.lodMin_ == static_cast<IRRender::LodLevel>(i == 0 ? 0 : 4) &&
                        part.lodMax_ == part.lodMin_;
        }
        const auto count = [](const IRRender::ComparisonImage &image, bool blue) {
            std::size_t result = 0;
            for (const auto color : image.pixels_) {
                const int dominant = blue ? color.blue_ : color.red_;
                const int other = blue ? color.red_ : color.blue_;
                result +=
                    dominant > 20 && dominant * 2 > color.green_ * 3 && dominant * 2 > other * 3;
            }
            return result;
        };
        bool captureValid = captures.complete() && !captures.captureClipped();
        std::size_t nativeFine = 0, nativeCoarse = 0, commonFine = 0, commonCoarse = 0;
        if (captureValid) {
            nativeFine = count(captures.images()[0], false);
            nativeCoarse = count(captures.images()[4], true);
            const auto fine =
                IRRender::presentComparisonImage(captures.images()[0], {128, 96}, 1, {}, 8);
            const auto coarse =
                IRRender::presentComparisonImage(captures.images()[4], {128, 96}, 16, {}, 8);
            commonFine = count(fine.image_, false);
            commonCoarse = count(coarse.image_, true);
            captureValid = nativeCoarse > 4 && nativeFine > nativeCoarse * 32 && commonCoarse > 4 &&
                           commonFine * 2 > commonCoarse * 3;
            for (std::size_t i = 1; i < 4; ++i)
                captureValid = captureValid && count(captures.images()[i], false) == 0 &&
                               count(captures.images()[i], true) == 0;
        }
        bool onionEndpoint = shot != 3 && shot != 4;
        if (!onionEndpoint) {
            for (const auto id : panel.widgets()) {
                const auto image = IREntity::getComponentOptional<IRComponents::C_WidgetImage>(id);
                const auto position =
                    IREntity::getComponentOptional<IRComponents::C_GuiPosition>(id);
                if (!image || !position || (*position)->pos_ != IRMath::ivec2(228, 490))
                    continue;
                bool hasInk = false, correctChannels = true;
                for (const auto color : (*image)->texels_) {
                    hasInk = hasInk || color.blue_ > 20;
                    correctChannels =
                        correctChannels && (shot == 3 ? color.red_ == 0 : color.green_ == 0);
                }
                onionEndpoint = hasInk && correctChannels;
            }
        }
        const bool viewRestored =
            IRMath::length(IRRender::getCameraPosition2DIso() - view_.cameraIso_) < .001f &&
            IRMath::abs(IRRender::getCameraZoom().x - view_.zoom_) < .001f &&
            IRMath::abs(IRPrefab::Camera::getYaw() - view_.yaw_) < .001f;
        bool sameCaptureAfterNavigation = true;
        if ((shot == 1 || shot == 7) && captures.complete()) {
            for (std::size_t tier = 0; tier < captures.images().size(); ++tier) {
                std::uint64_t hash = 14695981039346656037ull;
                for (const auto color : captures.images()[tier].pixels_) {
                    hash ^= color.toPackedRGBA();
                    hash *= 1099511628211ull;
                }
                if (shot == 1)
                    disturbedCaptureHashes_[tier] = hash;
                else
                    sameCaptureAfterNavigation =
                        sameCaptureAfterNavigation && disturbedCaptureHashes_[tier] == hash;
            }
        }
        bool pass = unchanged;
        if (shot == 0)
            pass = pass && !active;
        if (shot >= 1 && shot <= 4)
            pass = pass && active && captureValid && viewRestored && scene.parts().size() == 2;
        if (shot == 2)
            pass = pass && panel.selectedTier() == IRRender::LodLevel::LOD_4 &&
                   panel.referenceTier() == IRRender::LodLevel::LOD_0;
        if (shot == 3)
            pass = pass && panel.opacity() == 0 && onionEndpoint;
        if (shot == 4)
            pass = pass && panel.opacity() == 1 && onionEndpoint;
        if (shot == 5) {
            pass = pass && !active && viewRestored && scene.parts().size() == 3;
            if (scene.parts().size() == 3) {
                const auto &part = scene.parts().back();
                const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(part.entity_);
                const auto &pose =
                    IREntity::getComponent<IRComponents::C_LocalTransform>(part.entity_);
                pass = pass && part.lodMin_ == IRRender::LodLevel::LOD_2 &&
                       part.lodMax_ == IRRender::LodLevel::LOD_2 &&
                       set.size_ == IRMath::ivec3(6, 4, 3) &&
                       set.anchor_ == IRComponents::EntityAnchor::CENTER &&
                       authoredVoxelFingerprint(set) == fingerprints_[0] &&
                       pose.translation_ == poses_[0].translation_ &&
                       pose.rotation_ == poses_[0].rotation_ && pose.scale_ == poses_[0].scale_;
            }
        }
        if (shot == 6 || shot == 8)
            pass = pass && !active && viewRestored && scene.parts().size() == 2;
        if (shot == 7)
            pass = pass && active && captureValid && viewRestored && sameCaptureAfterNavigation &&
                   scene.parts().size() == 2;
        std::ostringstream out;
        out << "shot=" << shot << " authored=" << unchanged << " active=" << active
            << " captures=" << captures.captured()
            << " apertureClipped=" << captures.captureClipped() << " native=" << nativeFine << '/'
            << nativeCoarse << " common=" << commonFine << '/' << commonCoarse
            << " selected=" << static_cast<int>(panel.selectedTier())
            << " reference=" << static_cast<int>(panel.referenceTier())
            << " opacity=" << panel.opacity() << " parts=" << scene.parts().size()
            << " viewRestored=" << viewRestored
            << " navigationStable=" << sameCaptureAfterNavigation;
        actual = out.str();
        return pass;
    }
};

namespace Session {
bool evaluateLodWorkflowCheck(const void *context, std::string &actual);

inline Recipe buildLodWorkflow() {
    Recipe recipe;
    recipe.name_ = "lod_workflow";
    recipe.referenceFurniture_ = false;
    static constexpr std::array checks{0, 1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array labels{
        "authored_variants",
        "native_and_common",
        "onion_two_tiers",
        "onion_reference",
        "onion_selected",
        "derive_same_extent",
        "undo_derive",
        "reopen_comparison",
        "done_restores_view"
    };
    using Type = IRVideo::GuiInputEvent::Type;
    for (const int shot : checks) {
        Segment segment;
        segment.label_ = "lod_" + std::string(labels[shot]);
        const auto button = [&](int frame, Type type, IRInput::KeyMouseButtons key) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = type;
            event.button_ = key;
            segment.events_.push_back(event);
        };
        const auto move = [&](int frame, IRMath::vec2 point) {
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            button(frame, Type::MOVE, IRInput::kMouseButtonNull);
        };
        const auto click = [&](int frame, IRMath::vec2 point) {
            move(frame, point);
            button(frame + 1, Type::PRESS, IRInput::kMouseButtonLeft);
            button(frame + 2, Type::RELEASE, IRInput::kMouseButtonLeft);
        };
        if (shot == 1 || shot == 7) {
            click(0, {957, 332});
            if (shot == 1) {
                move(3, {800, 450});
                button(4, Type::SCROLL, IRInput::kMouseButtonNull);
                segment.events_.back().scroll_ = {0, 1};
                button(6, Type::PRESS, IRInput::kMouseButtonRight);
                move(8, {860, 450});
                move(12, {920, 450});
                button(15, Type::RELEASE, IRInput::kMouseButtonRight);
            }
            move(50, {920, 700});
        } else if (shot == 2) {
            click(0, {588, 516});
            click(5, {376, 548});
        } else if (shot == 3) {
            move(0, {702, 512});
            button(1, Type::PRESS, IRInput::kMouseButtonLeft);
            move(4, {600, 512});
            button(6, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else if (shot == 4) {
            move(24, {616, 512});
            button(25, Type::PRESS, IRInput::kMouseButtonLeft);
            move(28, {804, 512});
            button(30, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else if (shot == 5) {
            click(0, {482, 516});
            click(5, {390, 209});
        } else if (shot == 6) {
            button(0, Type::PRESS, IRInput::kKeyButtonLeftControl);
            button(2, Type::PRESS, IRInput::kKeyButtonZ);
            button(4, Type::RELEASE, IRInput::kKeyButtonZ);
            button(6, Type::RELEASE, IRInput::kKeyButtonLeftControl);
        } else if (shot == 8)
            click(0, {500, 209});
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateLodWorkflowCheck,
                &checks[shot],
                "lod_real_captures_extent_and_isolation"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}
} // namespace Session
} // namespace IRVoxelEditor

#endif
