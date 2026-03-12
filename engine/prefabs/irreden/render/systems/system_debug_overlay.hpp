#ifndef SYSTEM_DEBUG_OVERLAY_H
#define SYSTEM_DEBUG_OVERLAY_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/vertex_attributes.hpp>

#include <cmath>
#include <vector>

using namespace IRMath;

namespace IRDebug {

struct DebugVertex {
    float x, y;
    float r, g, b, a;
};

struct DebugLine3D {
    vec3 from;
    vec3 to;
    float r, g, b, a;
};

struct DebugCircle3D {
    vec3 center;
    float radius;
    float r, g, b, a;
    int segments;
};

struct DebugTriangle3D {
    vec3 a;
    vec3 b;
    vec3 c;
    float r, g, bColor, aColor;
};

struct DebugLine2D {
    vec2 from;
    vec2 to;
    float r, g, b, a;
};

struct DebugTriangle2D {
    vec2 a;
    vec2 b;
    vec2 c;
    float r, g, bColor, aColor;
};

inline std::vector<DebugLine3D> &getLines() {
    static std::vector<DebugLine3D> lines;
    return lines;
}

inline std::vector<DebugCircle3D> &getCircles() {
    static std::vector<DebugCircle3D> circles;
    return circles;
}

inline std::vector<DebugTriangle3D> &getTriangles() {
    static std::vector<DebugTriangle3D> triangles;
    return triangles;
}

inline std::vector<DebugLine2D> &getScreenLines() {
    static std::vector<DebugLine2D> lines;
    return lines;
}

inline std::vector<DebugTriangle2D> &getScreenTriangles() {
    static std::vector<DebugTriangle2D> triangles;
    return triangles;
}

inline void drawLine3D(vec3 from, vec3 to, float r, float g, float b, float a = 1.0f) {
    getLines().push_back({from, to, r, g, b, a});
}

inline void drawCircle3D(
    vec3 center, float radius, float r, float g, float b, float a = 1.0f, int segments = 32
) {
    getCircles().push_back({center, radius, r, g, b, a, segments});
}

inline void drawTriangle3D(
    vec3 a,
    vec3 b,
    vec3 c,
    float r,
    float g,
    float bColor,
    float aColor = 1.0f
) {
    getTriangles().push_back({a, b, c, r, g, bColor, aColor});
}

inline void drawLineScreen(vec2 from, vec2 to, float r, float g, float b, float a = 1.0f) {
    getScreenLines().push_back({from, to, r, g, b, a});
}

inline void drawTriangleScreen(
    vec2 a,
    vec2 b,
    vec2 c,
    float r,
    float g,
    float bColor,
    float aColor = 1.0f
) {
    getScreenTriangles().push_back({a, b, c, r, g, bColor, aColor});
}

inline void drawRectScreen(const vec2 min, const vec2 max, const vec4 fillColor, const vec4 borderColor) {
    const vec2 topLeft(min.x, max.y);
    const vec2 topRight(max.x, max.y);
    const vec2 bottomLeft(min.x, min.y);
    const vec2 bottomRight(max.x, min.y);

    drawTriangleScreen(
        bottomLeft, bottomRight, topRight,
        fillColor.r, fillColor.g, fillColor.b, fillColor.a
    );
    drawTriangleScreen(
        bottomLeft, topRight, topLeft,
        fillColor.r, fillColor.g, fillColor.b, fillColor.a
    );

    drawLineScreen(bottomLeft, bottomRight, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    drawLineScreen(bottomRight, topRight, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    drawLineScreen(topRight, topLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    drawLineScreen(topLeft, bottomLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
}

inline void drawDiamond3D(vec3 center, float radius, float r, float g, float b, float a = 1.0f) {
    const vec3 xPos = center + vec3(radius, 0.0f, 0.0f);
    const vec3 yPos = center + vec3(0.0f, radius, 0.0f);
    const vec3 xNeg = center + vec3(-radius, 0.0f, 0.0f);
    const vec3 yNeg = center + vec3(0.0f, -radius, 0.0f);

    drawTriangle3D(xPos, yPos, xNeg, r, g, b, a);
    drawTriangle3D(xPos, xNeg, yNeg, r, g, b, a);
}

inline void drawPath3D(
    const std::vector<vec3> &points, float r, float g, float b, float a = 1.0f
) {
    for (size_t i = 0; i + 1 < points.size(); i++) {
        drawLine3D(points[i], points[i + 1], r, g, b, a);
    }
}

inline void clear() {
    getLines().clear();
    getCircles().clear();
    getTriangles().clear();
    getScreenLines().clear();
    getScreenTriangles().clear();
}

inline vec2 worldToScreen(vec3 worldPos) {
    vec2 stepSize = vec2(IRRender::getTriangleStepSizeScreen());
    vec2 viewport = vec2(IRRender::getViewport());
    vec2 screenCenter = viewport * 0.5f;
    vec2 camIso = IRRender::getCameraPosition2DIso();

    vec2 posIso = pos3DtoPos2DIso(worldPos);
    vec2 relIso = posIso + camIso;
    vec2 screenOffset = relIso * stepSize * vec2(1.0f, -1.0f);
    return screenCenter + screenOffset;
}

inline vec3 screenToWorld(vec2 screenPos, float zLevel = 0.0f) {
    vec2 stepSize = vec2(IRRender::getTriangleStepSizeScreen());
    vec2 viewport = vec2(IRRender::getViewport());
    vec2 screenCenter = viewport * 0.5f;
    vec2 camIso = IRRender::getCameraPosition2DIso();

    vec2 screenOffset = screenPos - screenCenter;
    vec2 relIso = screenOffset / stepSize * vec2(1.0f, -1.0f);
    vec2 posIso = relIso - camIso;

    const float zShift = 2.0f * zLevel;
    const float x = -0.5f * (posIso.x + posIso.y - zShift);
    const float y = 0.5f * (posIso.x - posIso.y + zShift);
    return vec3(x, y, zLevel);
}

} // namespace IRDebug

namespace IRSystem {

template <> struct System<DEBUG_OVERLAY> {
    static SystemId create() {
        struct DebugOverlayUBO {
            mat4 mvp;
        };

        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "DebugOverlayProgram",
            std::vector{
                IRRender::ShaderStage{IRRender::kFileVertDebugOverlay, GL_VERTEX_SHADER}.getHandle(),
                IRRender::ShaderStage{IRRender::kFileFragDebugOverlay, GL_FRAGMENT_SHADER}.getHandle()
            }
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "DebugOverlayUBO",
            nullptr,
            sizeof(DebugOverlayUBO),
            GL_DYNAMIC_STORAGE_BIT,
            GL_UNIFORM_BUFFER,
            IRRender::kBufferIndex_DebugOverlayData
        );

        constexpr size_t kMaxVertices = 512 * 1024;
        auto [vbId, vb] = IRRender::createNamedResource<IRRender::Buffer>(
            "DebugOverlayVB",
            nullptr,
            kMaxVertices * sizeof(IRDebug::DebugVertex),
            GL_DYNAMIC_STORAGE_BIT
        );

        IRRender::createNamedResource<IRRender::VAO>(
            "DebugOverlayVAO",
            vb->getHandle(),
            0,
            2,
            IRRender::kAttrListDebugVertex
        );

        // Use C_Name as the tick archetype (matches a few canvas entities).
        // The tick body is a no-op; all real work is in endTick.
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
                if (lines.empty() && circles.empty() && triangles.empty() &&
                    screenLines.empty() && screenTriangles.empty()) {
                    return;
                }

                std::vector<IRDebug::DebugVertex> triangleVertices;
                std::vector<IRDebug::DebugVertex> lineVertices;
                triangleVertices.reserve((triangles.size() + screenTriangles.size()) * 3);
                lineVertices.reserve((lines.size() + screenLines.size()) * 2 + circles.size() * 64);

                for (const auto &triangle : triangles) {
                    vec2 a = IRDebug::worldToScreen(triangle.a);
                    vec2 b = IRDebug::worldToScreen(triangle.b);
                    vec2 c = IRDebug::worldToScreen(triangle.c);
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
                    vec2 a = IRDebug::worldToScreen(line.from);
                    vec2 b = IRDebug::worldToScreen(line.to);
                    lineVertices.push_back({a.x, a.y, line.r, line.g, line.b, line.a});
                    lineVertices.push_back({b.x, b.y, line.r, line.g, line.b, line.a});
                }

                for (const auto &triangle : screenTriangles) {
                    triangleVertices.push_back(
                        {triangle.a.x, triangle.a.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {triangle.b.x, triangle.b.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                    triangleVertices.push_back(
                        {triangle.c.x, triangle.c.y, triangle.r, triangle.g, triangle.bColor, triangle.aColor}
                    );
                }

                for (const auto &line : screenLines) {
                    lineVertices.push_back({line.from.x, line.from.y, line.r, line.g, line.b, line.a});
                    lineVertices.push_back({line.to.x, line.to.y, line.r, line.g, line.b, line.a});
                }

                for (const auto &circle : circles) {
                    int segs = circle.segments;
                    for (int i = 0; i < segs; i++) {
                        float angle0 = static_cast<float>(i) * (2.0f * 3.14159265f / segs);
                        float angle1 = static_cast<float>(i + 1) * (2.0f * 3.14159265f / segs);
                        vec3 p0 = circle.center + vec3(
                            std::cos(angle0) * circle.radius,
                            std::sin(angle0) * circle.radius,
                            0.0f);
                        vec3 p1 = circle.center + vec3(
                            std::cos(angle1) * circle.radius,
                            std::sin(angle1) * circle.radius,
                            0.0f);
                        vec2 s0 = IRDebug::worldToScreen(p0);
                        vec2 s1 = IRDebug::worldToScreen(p1);
                        lineVertices.push_back({s0.x, s0.y, circle.r, circle.g, circle.b, circle.a});
                        lineVertices.push_back({s1.x, s1.y, circle.r, circle.g, circle.b, circle.a});
                    }
                }

                if (triangleVertices.empty() && lineVertices.empty()) return;

                constexpr size_t kMaxVerts = 512 * 1024;
                if (triangleVertices.size() > kMaxVerts) {
                    triangleVertices.resize((kMaxVerts / 3) * 3);
                }
                if (lineVertices.size() > kMaxVerts) {
                    lineVertices.resize((kMaxVerts / 2) * 2);
                }

                auto *vbuf = IRRender::getNamedResource<IRRender::Buffer>("DebugOverlayVB");
                ivec2 vp = IRRender::getViewport();
                mat4 projection = IRMath::ortho(
                    0.0f, static_cast<float>(vp.x),
                    0.0f, static_cast<float>(vp.y),
                    -1.0f, 1.0f);
                DebugOverlayUBO ubo{projection};
                IRRender::getNamedResource<IRRender::Buffer>("DebugOverlayUBO")
                    ->subData(0, sizeof(DebugOverlayUBO), &ubo);

                IRRender::getNamedResource<IRRender::ShaderProgram>("DebugOverlayProgram")->use();
                IRRender::getNamedResource<IRRender::VAO>("DebugOverlayVAO")->bind();

                ENG_API->glDisable(GL_DEPTH_TEST);
                ENG_API->glDepthMask(GL_FALSE);
                ENG_API->glEnable(GL_BLEND);
                ENG_API->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                if (!triangleVertices.empty()) {
                    vbuf->subData(
                        0,
                        static_cast<GLsizeiptr>(
                            triangleVertices.size() * sizeof(IRDebug::DebugVertex)
                        ),
                        triangleVertices.data()
                    );
                    ENG_API->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVertices.size()));
                }

                if (!lineVertices.empty()) {
                    vbuf->subData(
                        0,
                        static_cast<GLsizeiptr>(lineVertices.size() * sizeof(IRDebug::DebugVertex)),
                        lineVertices.data()
                    );
                    ENG_API->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));
                }

                ENG_API->glDisable(GL_BLEND);
                ENG_API->glDepthMask(GL_TRUE);
                ENG_API->glEnable(GL_DEPTH_TEST);
                IRDebug::clear();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_OVERLAY_H */
