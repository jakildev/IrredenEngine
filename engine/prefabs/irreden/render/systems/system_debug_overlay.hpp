#ifndef SYSTEM_DEBUG_OVERLAY_H
#define SYSTEM_DEBUG_OVERLAY_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/vertex_attributes.hpp>

// The `namespace IRDebug` draw surface this system flushes. It sits in its own
// header so non-render TUs (the Lua binding) can issue draws without the GPU
// headers above; the buffers are shared across TUs by inline-function
// static-local deduplication. See #2375.
#include <irreden/render/debug_overlay_draws.hpp>

#include <vector>

using namespace IRMath;

namespace IRSystem {

template <> struct System<DEBUG_OVERLAY> {
    static SystemId create() {
        struct DebugOverlayUBO {
            mat4 mvp;
        };

        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "DebugOverlayProgram",
            std::vector{
                IRRender::ShaderStage{
                    IRRender::kFileVertDebugOverlay,
                    IRRender::ShaderType::VERTEX
                },
                IRRender::ShaderStage{
                    IRRender::kFileFragDebugOverlay,
                    IRRender::ShaderType::FRAGMENT
                }
            }
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "DebugOverlayUBO",
            nullptr,
            sizeof(DebugOverlayUBO),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_DebugOverlayData
        );

        auto [vbId, vb] = IRRender::createNamedResource<IRRender::Buffer>(
            "DebugOverlayVB",
            nullptr,
            IRDebug::kDebugOverlayMaxVertices * sizeof(IRDebug::DebugVertex),
            IRRender::BUFFER_STORAGE_DYNAMIC
        );

        IRRender::createNamedResource<IRRender::VAO>(
            "DebugOverlayVAO",
            vb,
            static_cast<const IRRender::Buffer *>(nullptr),
            2,
            IRRender::kAttrListDebugVertex
        );

        return createSystem<C_Name>(
            "DebugOverlay",
            [](const C_Name &) {},
            nullptr,
            []() {
                auto &lines = IRDebug::getLines();
                auto &circles = IRDebug::getCircles();
                auto &triangles = IRDebug::getTriangles();
                auto &screenLines = IRDebug::getScreenLines();
                auto &screenTriangles = IRDebug::getScreenTriangles();
                if (lines.empty() && circles.empty() && triangles.empty() && screenLines.empty() &&
                    screenTriangles.empty()) {
                    return;
                }

                IRDebug::WorldToScreenCache w2s;
                w2s.refresh();

                static std::vector<IRDebug::DebugVertex> triangleVertices;
                static std::vector<IRDebug::DebugVertex> lineVertices;
                triangleVertices.clear();
                lineVertices.clear();
                triangleVertices.reserve((triangles.size() + screenTriangles.size()) * 3);
                lineVertices.reserve((lines.size() + screenLines.size()) * 2 + circles.size() * 32);

                for (const auto &triangle : triangles) {
                    vec2 a = w2s.project(triangle.a);
                    vec2 b = w2s.project(triangle.b);
                    vec2 c = w2s.project(triangle.c);
                    triangleVertices.push_back(
                        {a.x, a.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {b.x, b.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {c.x, c.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                }

                for (const auto &line : lines) {
                    vec2 a = w2s.project(line.from);
                    vec2 b = w2s.project(line.to);
                    lineVertices.push_back({a.x, a.y, line.r, line.g, line.b, line.a});
                    lineVertices.push_back({b.x, b.y, line.r, line.g, line.b, line.a});
                }

                for (const auto &triangle : screenTriangles) {
                    triangleVertices.push_back(
                        {triangle.a.x,
                         triangle.a.y,
                         triangle.r,
                         triangle.g,
                         triangle.bColor,
                         triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {triangle.b.x,
                         triangle.b.y,
                         triangle.r,
                         triangle.g,
                         triangle.bColor,
                         triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {triangle.c.x,
                         triangle.c.y,
                         triangle.r,
                         triangle.g,
                         triangle.bColor,
                         triangle.aColor}
                    );
                }

                for (const auto &line : screenLines) {
                    lineVertices.push_back(
                        {line.from.x, line.from.y, line.r, line.g, line.b, line.a}
                    );
                    lineVertices.push_back({line.to.x, line.to.y, line.r, line.g, line.b, line.a});
                }

                const auto &lut = IRDebug::getCircleLut();
                for (const auto &circle : circles) {
                    const int segs = std::min(circle.segments, IRDebug::kCircleLutMaxSegments);
                    const int step = IRDebug::kCircleLutMaxSegments / segs;
                    for (int i = 0; i < segs; i++) {
                        const int idx0 = (i * step) % IRDebug::kCircleLutMaxSegments;
                        const int idx1 = ((i + 1) * step) % IRDebug::kCircleLutMaxSegments;
                        vec3 p0 = circle.center + vec3(
                                                      lut.cosTable[idx0] * circle.radius,
                                                      lut.sinTable[idx0] * circle.radius,
                                                      0.0f
                                                  );
                        vec3 p1 = circle.center + vec3(
                                                      lut.cosTable[idx1] * circle.radius,
                                                      lut.sinTable[idx1] * circle.radius,
                                                      0.0f
                                                  );
                        vec2 s0 = w2s.project(p0);
                        vec2 s1 = w2s.project(p1);
                        lineVertices.push_back(
                            {s0.x, s0.y, circle.r, circle.g, circle.b, circle.a}
                        );
                        lineVertices.push_back(
                            {s1.x, s1.y, circle.r, circle.g, circle.b, circle.a}
                        );
                    }
                }

                if (triangleVertices.empty() && lineVertices.empty())
                    return;

                if (triangleVertices.size() + lineVertices.size() >
                    IRDebug::kDebugOverlayMaxVertices) {
                    const size_t triMax = (IRDebug::kDebugOverlayTriangleVertexBudget /
                                           IRDebug::kDebugOverlayTriangleVertexCount) *
                                          IRDebug::kDebugOverlayTriangleVertexCount;
                    if (triangleVertices.size() > triMax) {
                        triangleVertices.resize(triMax);
                    }
                    const size_t lineMax =
                        ((IRDebug::kDebugOverlayMaxVertices - triangleVertices.size()) /
                         IRDebug::kDebugOverlayLineVertexCount) *
                        IRDebug::kDebugOverlayLineVertexCount;
                    if (lineVertices.size() > lineMax) {
                        lineVertices.resize(lineMax);
                    }
                }

                auto *vbuf = IRRender::getNamedResource<IRRender::Buffer>("DebugOverlayVB");

                const auto triBytes = static_cast<std::ptrdiff_t>(
                    triangleVertices.size() * sizeof(IRDebug::DebugVertex)
                );
                const auto lineBytes =
                    static_cast<std::ptrdiff_t>(lineVertices.size() * sizeof(IRDebug::DebugVertex));

                if (!triangleVertices.empty()) {
                    vbuf->subData(0, static_cast<std::size_t>(triBytes), triangleVertices.data());
                }
                if (!lineVertices.empty()) {
                    vbuf->subData(
                        triBytes,
                        static_cast<std::size_t>(lineBytes),
                        lineVertices.data()
                    );
                }

                ivec2 vp = IRRender::getViewport();
                mat4 projection = IRMath::ortho(
                    0.0f,
                    static_cast<float>(vp.x),
                    0.0f,
                    static_cast<float>(vp.y),
                    -1.0f,
                    1.0f
                );
                DebugOverlayUBO ubo{projection};
                IRRender::getNamedResource<IRRender::Buffer>("DebugOverlayUBO")
                    ->subData(0, sizeof(DebugOverlayUBO), &ubo);

                IRRender::getNamedResource<IRRender::ShaderProgram>("DebugOverlayProgram")->use();
                IRRender::getNamedResource<IRRender::VAO>("DebugOverlayVAO")->bind();

                IRRender::device()->setDepthTest(false);
                IRRender::device()->setDepthWrite(false);
                IRRender::device()->enableBlending();

                if (!triangleVertices.empty()) {
                    IRRender::device()->drawArrays(
                        IRRender::DrawMode::TRIANGLES,
                        0,
                        static_cast<int>(triangleVertices.size())
                    );
                }

                if (!lineVertices.empty()) {
                    IRRender::device()->drawArrays(
                        IRRender::DrawMode::LINES,
                        static_cast<int>(triangleVertices.size()),
                        static_cast<int>(lineVertices.size())
                    );
                }

                IRRender::device()->disableBlending();
                IRRender::device()->setDepthWrite(true);
                IRRender::device()->setDepthTest(true);
                IRDebug::clear();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_OVERLAY_H */
