#ifndef IR_PREFAB_DEBUG_OVERLAY_DRAWS_H
#define IR_PREFAB_DEBUG_OVERLAY_DRAWS_H

// Immediate-mode debug-overlay draw surface (`namespace IRDebug`). Lives apart
// from the `System<DEBUG_OVERLAY>` flush that consumes it so non-render
// translation units — notably the Lua binding in
// `engine/script/include/irreden/script/lua_debug_overlay_bindings.hpp` — can
// issue draws without pulling in the flush's GPU headers (`buffer.hpp` /
// `shader.hpp` / `vao.hpp`). See #2375.
//
// Every draw here is pure CPU buffering: it appends a record to one of the
// five inline-static vectors below and performs no render work. The
// `System<DEBUG_OVERLAY>` flush (the system header, which includes this one)
// owns all GPU work AND owns clearing — it consumes and clears every buffer
// each RENDER tick.
//
// IMMEDIATE MODE: a draw survives exactly one flush. It persists on screen
// only if re-issued every frame by a system ordered BEFORE `DEBUG_OVERLAY` in
// the RENDER pipeline.
//
// The draw / accessor functions must stay `inline` (same namespace,
// non-`static`): the single buffer set is shared across translation units by
// inline-function static-local deduplication. Making them `static` would give
// each TU its own dead buffers, and a Lua-issued draw would silently never
// reach the flush.
//
// Math types are explicitly `IRMath::`-qualified here. The system header's
// file-scope `using namespace IRMath;` deliberately stays behind in that
// header, so including this one does not inject the namespace into a
// consumer's TU.

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <array>
#include <vector>

namespace IRDebug {

struct DebugVertex {
    float x, y;
    float r, g, b, a;
};

struct DebugLine3D {
    IRMath::vec3 from;
    IRMath::vec3 to;
    float r, g, b, a;
};

struct DebugCircle3D {
    IRMath::vec3 center;
    float radius;
    float r, g, b, a;
    int segments;
};

struct DebugTriangle3D {
    IRMath::vec3 a;
    IRMath::vec3 b;
    IRMath::vec3 c;
    float r, g, bColor, aColor;
};

struct DebugLine2D {
    IRMath::vec2 from;
    IRMath::vec2 to;
    float r, g, b, a;
};

struct DebugTriangle2D {
    IRMath::vec2 a;
    IRMath::vec2 b;
    IRMath::vec2 c;
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

inline void
drawLine3D(IRMath::vec3 from, IRMath::vec3 to, float r, float g, float b, float a = 1.0f) {
    getLines().push_back({from, to, r, g, b, a});
}

inline void drawCircle3D(
    IRMath::vec3 center, float radius, float r, float g, float b, float a = 1.0f, int segments = 32
) {
    getCircles().push_back({center, radius, r, g, b, a, segments});
}

inline void drawTriangle3D(
    IRMath::vec3 a,
    IRMath::vec3 b,
    IRMath::vec3 c,
    float r,
    float g,
    float bColor,
    float aColor = 1.0f
) {
    getTriangles().push_back({a, b, c, r, g, bColor, aColor});
}

inline void
drawLineScreen(IRMath::vec2 from, IRMath::vec2 to, float r, float g, float b, float a = 1.0f) {
    getScreenLines().push_back({from, to, r, g, b, a});
}

inline void drawTriangleScreen(
    IRMath::vec2 a,
    IRMath::vec2 b,
    IRMath::vec2 c,
    float r,
    float g,
    float bColor,
    float aColor = 1.0f
) {
    getScreenTriangles().push_back({a, b, c, r, g, bColor, aColor});
}

inline void drawRectScreen(
    const IRMath::vec2 min,
    const IRMath::vec2 max,
    const IRMath::vec4 fillColor,
    const IRMath::vec4 borderColor
) {
    const IRMath::vec2 topLeft(min.x, max.y);
    const IRMath::vec2 topRight(max.x, max.y);
    const IRMath::vec2 bottomLeft(min.x, min.y);
    const IRMath::vec2 bottomRight(max.x, min.y);

    drawTriangleScreen(
        bottomLeft,
        bottomRight,
        topRight,
        fillColor.r,
        fillColor.g,
        fillColor.b,
        fillColor.a
    );
    drawTriangleScreen(
        bottomLeft,
        topRight,
        topLeft,
        fillColor.r,
        fillColor.g,
        fillColor.b,
        fillColor.a
    );

    drawLineScreen(
        bottomLeft,
        bottomRight,
        borderColor.r,
        borderColor.g,
        borderColor.b,
        borderColor.a
    );
    drawLineScreen(
        bottomRight,
        topRight,
        borderColor.r,
        borderColor.g,
        borderColor.b,
        borderColor.a
    );
    drawLineScreen(topRight, topLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    drawLineScreen(topLeft, bottomLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
}

inline void drawDotScreen(IRMath::vec2 center, float radius, IRMath::vec4 color) {
    drawTriangleScreen(
        center + IRMath::vec2(-radius, 0),
        center + IRMath::vec2(radius, 0),
        center + IRMath::vec2(0, radius),
        color.r,
        color.g,
        color.b,
        color.a
    );
    drawTriangleScreen(
        center + IRMath::vec2(-radius, 0),
        center + IRMath::vec2(0, -radius),
        center + IRMath::vec2(radius, 0),
        color.r,
        color.g,
        color.b,
        color.a
    );
}

inline void
drawDiamond3D(IRMath::vec3 center, float radius, float r, float g, float b, float a = 1.0f) {
    const IRMath::vec3 xPos = center + IRMath::vec3(radius, 0.0f, 0.0f);
    const IRMath::vec3 yPos = center + IRMath::vec3(0.0f, radius, 0.0f);
    const IRMath::vec3 xNeg = center + IRMath::vec3(-radius, 0.0f, 0.0f);
    const IRMath::vec3 yNeg = center + IRMath::vec3(0.0f, -radius, 0.0f);

    drawTriangle3D(xPos, yPos, xNeg, r, g, b, a);
    drawTriangle3D(xPos, xNeg, yNeg, r, g, b, a);
}

inline void
drawPath3D(const std::vector<IRMath::vec3> &points, float r, float g, float b, float a = 1.0f) {
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

inline IRMath::vec2 worldToScreen(IRMath::vec3 worldPos) {
    IRMath::vec2 stepSize = IRMath::vec2(IRRender::getTriangleStepSizeScreen());
    IRMath::vec2 viewport = IRMath::vec2(IRRender::getViewport());
    IRMath::vec2 screenCenter = viewport * 0.5f;
    IRMath::vec2 camIso = IRRender::getCameraPosition2DIso();

    IRMath::vec2 posIso = IRMath::pos3DtoPos2DIso(worldPos);
    IRMath::vec2 relIso = posIso + camIso;
    IRMath::vec2 screenOffset = IRMath::isoDeltaToScreenDelta(relIso, stepSize);
    return screenCenter + screenOffset;
}

inline IRMath::vec3 screenToWorld(IRMath::vec2 screenPos, float zLevel = 0.0f) {
    IRMath::vec2 stepSize = IRMath::vec2(IRRender::getTriangleStepSizeScreen());
    IRMath::vec2 viewport = IRMath::vec2(IRRender::getViewport());
    IRMath::vec2 screenCenter = viewport * 0.5f;
    IRMath::vec2 camIso = IRRender::getCameraPosition2DIso();

    IRMath::vec2 screenOffset = screenPos - screenCenter;
    IRMath::vec2 relIso = IRMath::screenDeltaToIsoDelta(screenOffset, stepSize);
    IRMath::vec2 posIso = relIso - camIso;

    const float zShift = 2.0f * zLevel;
    const float x = -0.5f * (posIso.x + posIso.y - zShift);
    const float y = 0.5f * (posIso.x - posIso.y + zShift);
    return IRMath::vec3(x, y, zLevel);
}

constexpr std::size_t kDebugOverlayMaxVertices = 512 * 1024;
constexpr std::size_t kDebugOverlayTriangleVertexCount = 3;
constexpr std::size_t kDebugOverlayLineVertexCount = 2;
// When we overflow the fixed upload buffer, keep half the budget available for lines.
constexpr std::size_t kDebugOverlayTriangleVertexBudget = kDebugOverlayMaxVertices / 2;

constexpr int kCircleLutMaxSegments = 32;

struct CircleLut {
    std::array<float, kCircleLutMaxSegments + 1> cosTable;
    std::array<float, kCircleLutMaxSegments + 1> sinTable;
};

inline const CircleLut &getCircleLut() {
    static const CircleLut lut = []() {
        CircleLut t{};
        for (int i = 0; i <= kCircleLutMaxSegments; ++i) {
            float angle = static_cast<float>(i) * (2.0f * 3.14159265f / kCircleLutMaxSegments);
            t.cosTable[i] = IRMath::cos(angle);
            t.sinTable[i] = IRMath::sin(angle);
        }
        return t;
    }();
    return lut;
}

struct WorldToScreenCache {
    IRMath::vec2 stepSize;
    IRMath::vec2 screenCenter;
    IRMath::vec2 camIso;
    IRMath::vec2 stepSizeFlipped;

    void refresh() {
        stepSize = IRMath::vec2(IRRender::getTriangleStepSizeScreen());
        IRMath::vec2 viewport = IRMath::vec2(IRRender::getViewport());
        screenCenter = viewport * 0.5f;
        camIso = IRRender::getCameraPosition2DIso();
        stepSizeFlipped = IRMath::isoDeltaToScreenDelta(IRMath::vec2(1.0f), stepSize);
    }

    IRMath::vec2 project(IRMath::vec3 worldPos) const {
        IRMath::vec2 posIso = IRMath::pos3DtoPos2DIso(worldPos);
        IRMath::vec2 relIso = posIso + camIso;
        return screenCenter + relIso * stepSizeFlipped;
    }
};

} // namespace IRDebug

#endif /* IR_PREFAB_DEBUG_OVERLAY_DRAWS_H */
