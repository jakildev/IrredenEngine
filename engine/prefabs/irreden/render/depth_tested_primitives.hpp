#ifndef DEPTH_TESTED_PRIMITIVES_H
#define DEPTH_TESTED_PRIMITIVES_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/framebuffer_screen_math.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/vertex_attributes.hpp>

#include <array>
#include <span>
#include <vector>

namespace IRPrefab {

namespace detail {

// Top-down output pixels: geometry must share the compositor's camera raster
// phase before the final nearest-neighbour upscale, including continuous zoom.
inline IRMath::vec2 primitiveCameraRasterCorrection(
    IRMath::vec2 cameraIso,
    IRMath::vec2 zoom,
    IRMath::ivec2 outputScale,
    const IRMath::CameraRasterPhase *frame
) {
    const IRMath::vec2 pitch(IRMath::cameraZoomPitch(zoom));
    const IRMath::vec2 detached = frame != nullptr
                                      ? IRMath::vec2(IRMath::cameraRasterDetachedOffset(*frame))
                                      : IRMath::cameraSnappedDetachedOffset(cameraIso, pitch);
    const IRMath::vec2 residual =
        frame != nullptr
            ? IRMath::vec2(IRMath::cameraRasterScreenResidual(*frame, outputScale))
            : IRMath::vec2(
                  IRMath::cameraSubPixelOffsets(cameraIso, zoom, outputScale).screenPxResidual_
              );
    const IRMath::vec2 rasterCamera =
        IRMath::floor(cameraIso) * pitch + detached * IRMath::vec2(1, -1);
    return (rasterCamera - cameraIso * pitch) * IRMath::vec2(outputScale) +
           residual * IRPlatform::kIsoToScreenSign;
}

} // namespace detail

struct WorldPrimitiveVertex {
    IRMath::vec3 position_;
    IRMath::vec4 color_;
};

// A translucent box contributes each visible face once. Back faces are omitted
// rather than blended through the front, and shared edges are emitted once.
inline void appendVisibleBox(
    IRMath::vec3 center,
    IRMath::vec3 halfExtents,
    IRMath::vec4 fillColor,
    IRMath::vec4 edgeColor,
    float visualYaw,
    std::vector<WorldPrimitiveVertex> &triangles,
    std::vector<WorldPrimitiveVertex> &lines
) {
    const auto faces =
        IRMath::visibleTriplet(IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), -visualYaw));
    std::array<bool, 64> emittedEdges{};
    const auto corner = [&](int bits) {
        IRMath::vec3 point = center;
        for (int axis = 0; axis < 3; ++axis)
            point[axis] += (bits & (1 << axis)) ? halfExtents[axis] : -halfExtents[axis];
        return point;
    };
    for (const auto face : faces) {
        const int axis = static_cast<int>(face) / 2;
        const int base = (static_cast<int>(face) & 1) << axis;
        const int u = 1 << ((axis + 1) % 3);
        const int v = 1 << ((axis + 2) % 3);
        const std::array<int, 4> corners{base, base | u, base | u | v, base | v};
        for (int index : {0, 1, 2, 0, 2, 3})
            triangles.push_back({corner(corners[index]), fillColor});
        for (int i = 0; i < 4; ++i) {
            const int a = corners[i];
            const int b = corners[(i + 1) % 4];
            const int key = IRMath::min(a, b) * 8 + IRMath::max(a, b);
            if (emittedEdges[key])
                continue;
            emittedEdges[key] = true;
            lines.push_back({corner(a), edgeColor});
            lines.push_back({corner(b), edgeColor});
        }
    }
}

// Same world depth band and yaw metric as the voxel framebuffer composite.
inline float worldPrimitiveDepth(
    IRMath::vec3 point, float yaw, int subdivisions, bool cardinalVoxelRaster = false
) {
    // Cardinal voxel micropositions are lower-corner raster coordinates in
    // the view frame. Scatter instead subtracts the half-cell anchor before
    // encoding. Restore that anchor only when comparing against the gather.
    const int rasterAnchor =
        cardinalVoxelRaster ? IRMath::pos3DtoDistance(
                                  IRMath::vec3(0.5f * subdivisions * IRRender::kDepthEncodeShift)
                              )
                            : 0;
    const int encoded = IRMath::clamp(
        IRRender::canvasWorldDepthOffset(
            point,
            yaw,
            subdivisions * IRRender::kDepthEncodeShift,
            IRRender::TrixelSampleLayout::SOURCE_FACES
        ) + rasterAnchor,
        IRRender::kDepthForegroundCeil + 1,
        IRConstants::kTrixelDistanceMaxDistance
    );
    return static_cast<float>(encoded - IRConstants::kTrixelDistanceMinDistance) /
           static_cast<float>(
               IRConstants::kTrixelDistanceMaxDistance - IRConstants::kTrixelDistanceMinDistance
           );
}

// Opt-in, render-manager-owned resources; call create once per pipeline lifetime.
// Draw after all main-framebuffer composites and before FRAMEBUFFER_TO_SCREEN.
// Cardinal depth follows main-world voxel raster coordinates; this path is for
// voxel authoring aids, not analytic/private-canvas surface compositing.
// Primitives test existing world depth, but never write depth, hover IDs, shadows,
// or lighting. The caller owns the queues and their frame lifetime.
class DepthTestedPrimitives {
  public:
    void create() {
        m_program = IRRender::createNamedResource<IRRender::ShaderProgram>(
                        "DepthTestedPrimitivesProgram",
                        std::vector{
                            IRRender::ShaderStage{
                                IRRender::kFileVertDepthTestedPrimitives,
                                IRRender::ShaderType::VERTEX
                            },
                            IRRender::ShaderStage{
                                IRRender::kFileFragDepthTestedPrimitives,
                                IRRender::ShaderType::FRAGMENT
                            }
                        }
        ).second;
        m_buffer = IRRender::createNamedResource<IRRender::Buffer>(
                       "DepthTestedPrimitivesVB",
                       nullptr,
                       sizeof(WorldPrimitiveVertex),
                       IRRender::BUFFER_STORAGE_DYNAMIC
        )
                       .second;
        const IRRender::VertexArrayAttribute attributes[]{
            IRRender::kAttrFloat3,
            IRRender::kAttrFloat4
        };
        m_vao = IRRender::createNamedResource<IRRender::VAO>(
                    "DepthTestedPrimitivesVAO",
                    m_buffer,
                    static_cast<const IRRender::Buffer *>(nullptr),
                    2,
                    attributes
        )
                    .second;
    }

    void draw(
        std::span<const WorldPrimitiveVertex> lines, std::span<const WorldPrimitiveVertex> triangles
    ) {
        if (lines.empty() && triangles.empty())
            return;
        const auto &framebuffer =
            IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
        const auto &transform =
            IREntity::getComponent<IRComponents::C_WorldTransform>("mainFramebuffer");
        const auto residual = Camera::screenResidual(
            IRRender::getEffectiveCameraIso(),
            IRRender::getCameraZoom(),
            IRRender::getOutputScaleFactor()
        );
        const auto model = IRSystem::detail::framebufferScreenModel(
            framebuffer.getResolutionPlusBuffer(),
            transform.translation_,
            residual,
            true
        );
        const IRMath::vec2 center(model[3]);
        const IRMath::vec2 extent(model[0][0], model[1][1]);
        const auto cameraIso = IRRender::getEffectiveCameraIso();
        const auto zoom = IRRender::getCameraZoom();
        const auto cameraCorrection = detail::primitiveCameraRasterCorrection(
            cameraIso,
            zoom,
            IRRender::getOutputScaleFactor(),
            Camera::zoomFrame(cameraIso, zoom)
        );
        const float yaw = Camera::getYaw();
        const int subdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
        const bool cardinalVoxelRaster = Camera::getYawSplit().second == 0.0f;
        m_vertices.clear();
        m_vertices.reserve(triangles.size() + lines.size());
        const auto append = [&](std::span<const WorldPrimitiveVertex> source) {
            for (const auto &vertex : source) {
                auto screen = IRRender::windowPointsToFramebufferPx(
                    IRRender::worldPos3DToWindowPointsExact(vertex.position_)
                );
                screen += cameraCorrection;
                screen.y = static_cast<float>(IRRender::getViewport().y) - screen.y;
                const auto clip = (screen - center) * 2.0f / extent;
                m_vertices.push_back(
                    {IRMath::vec3(
                         clip,
                         worldPrimitiveDepth(
                             vertex.position_,
                             yaw,
                             subdivisions,
                             cardinalVoxelRaster
                         )
                     ),
                     vertex.color_}
                );
            }
        };
        append(triangles);
        append(lines);
        const auto bytes = m_vertices.size() * sizeof(WorldPrimitiveVertex);
        if (bytes > m_capacity) {
            m_buffer->resize(bytes);
            // GL immutable-buffer growth changes its native name; refresh the
            // VAO binding as well. Metal resolves the Buffer on every bind.
            const IRRender::VertexArrayAttribute attributes[]{
                IRRender::kAttrFloat3,
                IRRender::kAttrFloat4
            };
            *m_vao = IRRender::VAO(m_buffer, nullptr, 2, attributes);
            m_capacity = bytes;
        }
        m_buffer->subData(0, bytes, m_vertices.data());
        framebuffer.bindFramebuffer();
        m_program->use();
        m_vao->bind();
        auto *device = IRRender::device();
        device->setDepthTest(true);
        device->setDepthWrite(false);
        device->enableBlending();
        if (!triangles.empty())
            device
                ->drawArrays(IRRender::DrawMode::TRIANGLES, 0, static_cast<int>(triangles.size()));
        if (!lines.empty())
            device->drawArrays(
                IRRender::DrawMode::LINES,
                static_cast<int>(triangles.size()),
                static_cast<int>(lines.size())
            );
        device->disableBlending();
        device->setDepthWrite(true);
    }

  private:
    IRRender::ShaderProgram *m_program = nullptr;
    IRRender::Buffer *m_buffer = nullptr;
    IRRender::VAO *m_vao = nullptr;
    std::size_t m_capacity = sizeof(WorldPrimitiveVertex);
    std::vector<WorldPrimitiveVertex> m_vertices;
};

} // namespace IRPrefab

#endif /* DEPTH_TESTED_PRIMITIVES_H */
