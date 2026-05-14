#ifndef IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H
#define IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H

/// GPU-format conversion for C_JointHierarchy.
///
/// Kept separate from component_joint_hierarchy.hpp so that the component
/// header (included by prefab_api.cpp and rig_bridge.hpp, which live in the
/// scripting layer) does not pull in IRRender types. Only files that
/// explicitly depend on IrredenEngineRendering should include this header.

#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <vector>

namespace IRPrefab::Rig {

/// Convert a runtime C_JointHierarchy to a flat GPU upload buffer.
/// Joint order is preserved; the resulting vector maps 1:1 to the
/// JointTransformBuffer SSBO slots consumed by c_shapes_to_trixel.
inline std::vector<IRRender::GPUJointTransform>
toGPUFormat(const IRComponents::C_JointHierarchy &h) {
    std::vector<IRRender::GPUJointTransform> gpu;
    gpu.reserve(h.joints_.size());
    for (const auto &j : h.joints_) {
        IRRender::GPUJointTransform t{};
        t.rotation = j.rotation_;
        t.translation = j.translation_;
        t.parentJointIndex = j.parentIndex_;
        gpu.push_back(t);
    }
    return gpu;
}

} // namespace IRPrefab::Rig

#endif /* IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H */
