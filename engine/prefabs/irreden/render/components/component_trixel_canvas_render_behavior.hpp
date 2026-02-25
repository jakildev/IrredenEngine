#ifndef COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_H
#define COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_H

namespace IRComponents {

struct C_TrixelCanvasRenderBehavior {
    bool useCameraPositionIso_;
    bool useCameraZoom_;
    bool applyRenderSubdivisions_;
    bool mouseHoverEnabled_;
    bool usePixelPerfectCameraOffset_;
    float parityOffsetIsoX_;
    float parityOffsetIsoY_;
    float staticPixelOffsetX_;
    float staticPixelOffsetY_;

    C_TrixelCanvasRenderBehavior(
        bool useCameraPositionIso,
        bool useCameraZoom,
        bool applyRenderSubdivisions,
        bool mouseHoverEnabled,
        bool usePixelPerfectCameraOffset,
        float parityOffsetIsoX,
        float parityOffsetIsoY,
        float staticPixelOffsetX,
        float staticPixelOffsetY
    )
        : useCameraPositionIso_{useCameraPositionIso}
        , useCameraZoom_{useCameraZoom}
        , applyRenderSubdivisions_{applyRenderSubdivisions}
        , mouseHoverEnabled_{mouseHoverEnabled}
        , usePixelPerfectCameraOffset_{usePixelPerfectCameraOffset}
        , parityOffsetIsoX_{parityOffsetIsoX}
        , parityOffsetIsoY_{parityOffsetIsoY}
        , staticPixelOffsetX_{staticPixelOffsetX}
        , staticPixelOffsetY_{staticPixelOffsetY} {}

    C_TrixelCanvasRenderBehavior()
        : C_TrixelCanvasRenderBehavior(
              true,
              true,
              true,
              true,
              true,
              0.0f,
              0.0f,
              0.0f,
              0.0f
          ) {}
};

} // namespace IRComponents

#endif /* COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_H */
