#ifndef SYSTEM_FOG_SUBJECT_ADOPT_SHAPE_H
#define SYSTEM_FOG_SUBJECT_ADOPT_SHAPE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_settled_gate.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <vector>

namespace IRSystem {

template <> struct System<FOG_SUBJECT_ADOPT_SHAPE> {
    static constexpr int kGrainSize = kDefaultGrainSize;
    IRPrefab::Fog::SettledGate gate_;

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::FrameDataFogObservers observers_{};
    IRComponents::FogLosColumnField los_{};
    IRComponents::C_FogRevealSettings settings_{};

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        fog_ = nullptr;
        observers_ = {};
        los_ = {};
        IRComponents::C_CanvasFogOfWar *fog = nullptr;
        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto attached =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog = *attached;
                fog_ = fog;
                IRPrefab::Fog::selectRevealSnapshot(
                    fog_->observers_,
                    fog_->losPublishedObservers_,
                    fog_->losField(),
                    observers_,
                    los_
                );
            }
        }
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        gate_.begin(activeCanvas_, nullptr, fog_, settings_);
        const std::vector<IREntity::ArchetypeNode *> nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::
                getArchetype<IRComponents::C_WorldTransform, IRComponents::C_ShapeDescriptor>(),
            gate_.queryExclude(
                IREntity::getArchetype<
                    IRComponents::C_FogRevealed,
                    IRComponents::C_FogField,
                    IRComponents::C_FogExempt>()
            )
        );
        if (fog != nullptr) {
            IRPrefab::Fog::touchShapeAnchorRegions(*fog, activeCanvas_, nodes);
        }
    }

    void tick(
        IREntity::EntityId &entity,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape
    ) const {
        if (fog_ == nullptr) {
            return;
        }
        if (!IRPrefab::Fog::isOnFogCanvas(shape, activeCanvas_)) {
            return;
        }

        IRComponents::C_FogRevealed revealed{};
        revealed.revealFactor_ =
            IRPrefab::Fog::evalReveal(*fog_, observers_, los_, worldTransform.translation_);
        revealed.shown_ = revealed.revealFactor_ >= settings_.showThreshold_;
        shape.fogBodyFactor_ = IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
        shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
        if (revealed.shown_) {
            shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        } else {
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
        }
        IREntity::setComponentDeferred(entity, revealed);
    }

    void tick(
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<IRComponents::C_WorldTransform> &worldTransforms,
        std::vector<IRComponents::C_ShapeDescriptor> &shapes
    ) {
        gate_.walkNode(*this, kGrainSize, type, entities, worldTransforms, shapes);
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_ADOPT_SHAPE,
            const IRComponents::C_WorldTransform,
            IRComponents::C_ShapeDescriptor,
            Exclude<IRComponents::C_FogRevealed>,
            Exclude<IRComponents::C_FogField>,
            Exclude<IRComponents::C_FogExempt>,
            GatesSettled,
            MainThread>("FogSubjectAdoptShape");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_ADOPT_SHAPE_H */
