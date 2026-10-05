#ifndef SYSTEM_FOG_SUBJECT_EXEMPT_SHAPE_H
#define SYSTEM_FOG_SUBJECT_EXEMPT_SHAPE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRSystem {

template <> struct System<FOG_SUBJECT_EXEMPT_SHAPE> {
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    bool enabled_ = false;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        enabled_ = activeCanvas_ != IREntity::kNullEntity &&
                   IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)
                       .has_value();
    }

    void tick(IRComponents::C_ShapeDescriptor &shape, const IRComponents::C_FogExempt &) const {
        if (!enabled_ || !IRPrefab::Fog::isOnFogCanvas(shape, activeCanvas_)) {
            return;
        }
        shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
        shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        shape.fogBodyFactor_ = 255;
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_EXEMPT_SHAPE,
            IRComponents::C_ShapeDescriptor,
            IRComponents::C_FogExempt,
            Exclude<IRComponents::C_FogRevealed>>("FogSubjectExemptShape");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_EXEMPT_SHAPE_H */
