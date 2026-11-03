#ifndef IR_VOXEL_EDITOR_ARRAY_PLACEMENT_H
#define IR_VOXEL_EDITOR_ARRAY_PLACEMENT_H

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>

namespace IRVoxelEditor {

// Array offsets live in the source's parent frame. The source stays outside
// the array; offsets describe only the new siblings.
inline IRComponents::C_LocalTransform arrayCopyTransform(
    IRComponents::C_LocalTransform source, const IRComponents::C_LocalTransform &offset
) {
    source.translation_ += offset.translation_;
    source.rotation_ = IRMath::quatMul(offset.rotation_, source.rotation_);
    return source;
}

inline IRComponents::C_WorldTransform arrayCopyWorldTransform(
    const IRComponents::C_LocalTransform &source,
    const IRComponents::C_LocalTransform &offset,
    const IRComponents::C_WorldTransform &parent
) {
    const auto local = arrayCopyTransform(source, offset);
    const auto world = IRMath::sqtCompose(
        {parent.scale_, parent.rotation_, parent.translation_},
        {local.scale_, local.rotation_, local.translation_}
    );
    return {world.translation_, world.rotation_, world.scale_};
}

} // namespace IRVoxelEditor

#endif
