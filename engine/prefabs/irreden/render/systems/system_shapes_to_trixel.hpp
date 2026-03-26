#ifndef SYSTEM_SHAPES_TO_TRIXEL_H
#define SYSTEM_SHAPES_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cstring>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

constexpr int kMaxShapeDescriptors = 8192;
constexpr std::uint32_t kBufferIndex_ShapesFrameData = 23;

template <> struct System<SHAPES_TO_TRIXEL> {
    static SystemId create() {
        static GPUShapesFrameData frameData{};
        using CanvasId = IREntity::EntityId;
        // TODO: replace with relation-based archetype grouping (RENDERS_ON)
        // so shapes are bucketed per archetype node, not per entity.
        static std::unordered_map<CanvasId, std::vector<GPUShapeDescriptor>> gpuShapesByCanvas;

        IRRender::createNamedResource<ShaderProgram>(
            "ShapesToTrixelProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompShapesToTrixel, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "ShapeDescriptorBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUShapeDescriptor),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_ShapeDescriptors
        );
        IRRender::createNamedResource<Buffer>(
            "ShapesFrameDataBuffer",
            nullptr,
            sizeof(GPUShapesFrameData),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_ShapesFrameData
        );
        IRRender::createNamedResource<Buffer>(
            "JointTransformBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUJointTransform),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_JointTransforms
        );
        IRRender::createNamedResource<Buffer>(
            "AnimationParamsBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUAnimationParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_AnimationParams
        );

        return createSystem<C_ShapeDescriptor, C_PositionGlobal3D>(
            "ShapesToTrixel",
            [](IREntity::EntityId &entityId,
               const C_ShapeDescriptor &shape,
               const C_PositionGlobal3D &pos) {
                CanvasId canvas = shape.canvasEntity_;
                if (canvas == IREntity::kNullEntity) {
                    canvas = IRRender::getActiveCanvasEntity();
                }

                auto &bucket = gpuShapesByCanvas[canvas];
                if (static_cast<int>(bucket.size()) >= kMaxShapeDescriptors) {
                    return;
                }
                GPUShapeDescriptor desc{};
                desc.worldPosition = vec4(pos.pos_, 1.0f);
                desc.params = shape.params_;
                desc.shapeType = static_cast<std::uint32_t>(shape.shapeType_);
                desc.color =
                    static_cast<std::uint32_t>(shape.color_.red_) |
                    (static_cast<std::uint32_t>(shape.color_.green_) << 8) |
                    (static_cast<std::uint32_t>(shape.color_.blue_) << 16) |
                    (static_cast<std::uint32_t>(shape.color_.alpha_) << 24);
                desc.entityId = entityId;
                desc.jointIndex = 0;
                desc.flags = shape.flags_;
                desc.lodLevel = shape.lodLevel_;
                bucket.push_back(desc);
            },
            []() {
                gpuShapesByCanvas.clear();
            },
            []() {
                IRRender::getNamedResource<ShaderProgram>("ShapesToTrixelProgram")->use();

                for (auto &[canvasId, gpuShapes] : gpuShapesByCanvas) {
                    if (gpuShapes.empty()) {
                        continue;
                    }

                    auto texturesOpt =
                        IREntity::getComponentOptional<C_TriangleCanvasTextures>(canvasId);
                    if (!texturesOpt.has_value()) {
                        continue;
                    }
                    auto &canvasTextures = *texturesOpt.value();

                    frameData.cameraTrixelOffset = IRRender::getCameraPosition2DIso();
                    frameData.trixelCanvasOffsetZ1 =
                        IRMath::trixelOriginOffsetZ1(canvasTextures.size_);
                    frameData.canvasSize = canvasTextures.size_;
                    frameData.shapeCount = static_cast<int>(gpuShapes.size());
                    frameData.voxelRenderOptions = ivec2(
                        static_cast<int>(IRRender::getVoxelRenderMode()),
                        IRRender::getVoxelRenderEffectiveSubdivisions()
                    );

                    IRRender::getNamedResource<Buffer>("ShapeDescriptorBuffer")
                        ->subData(
                            0,
                            gpuShapes.size() * sizeof(GPUShapeDescriptor),
                            gpuShapes.data()
                        );

                    const int shapeCount = static_cast<int>(gpuShapes.size());
                    const int groupsX = IRMath::min(shapeCount, 1024);
                    const int groupsY = (shapeCount + groupsX - 1) / groupsX;

                    canvasTextures.getTextureDistances()->bindAsImage(
                        1, TextureAccess::READ_WRITE, TextureFormat::R32I
                    );

                    frameData.passIndex = 0;
                    IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer")
                        ->subData(0, sizeof(GPUShapesFrameData), &frameData);
                    IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                    canvasTextures.getTextureColors()->bindAsImage(
                        0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                    );
                    canvasTextures.getTextureEntityIds()->bindAsImage(
                        2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI
                    );
                    frameData.passIndex = 1;
                    IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer")
                        ->subData(0, sizeof(GPUShapesFrameData), &frameData);
                    IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SHAPES_TO_TRIXEL_H */
