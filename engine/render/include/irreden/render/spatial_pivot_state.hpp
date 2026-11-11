#ifndef SPATIAL_PIVOT_STATE_H
#define SPATIAL_PIVOT_STATE_H

#include <irreden/ir_math.hpp>

namespace IRRender::detail {

class SpatialPivotState {
  public:
    void setFocus(IRMath::vec3 focusWorld) {
        m_focusWorld = focusWorld;
        m_hasFocus = true;
    }

    void clearFocus(bool retainAsDefault) {
        if (retainAsDefault && m_hasFocus) {
            m_defaultFocusWorld = m_focusWorld;
            m_hasDefault = true;
        }
        m_hasFocus = false;
    }

    void reseedDefault(IRMath::vec3 focusWorld) {
        m_defaultFocusWorld = focusWorld;
        m_hasDefault = true;
    }

    void discardDefault() {
        m_hasDefault = false;
    }

    bool hasFocus() const {
        return m_hasFocus;
    }

    bool hasDefault() const {
        return m_hasDefault;
    }

    IRMath::vec3 focus() const {
        return m_focusWorld;
    }

    IRMath::vec3 defaultFocus() const {
        return m_defaultFocusWorld;
    }

    IRMath::vec3 defaultFocusOr(IRMath::vec3 defaultFocusFallback) const {
        return m_hasDefault ? m_defaultFocusWorld : defaultFocusFallback;
    }

    IRMath::vec2 effectiveCameraIso(
        IRMath::vec2 cameraIso, IRMath::vec3 defaultFallback, IRMath::vec4 viewRotation
    ) const {
        const IRMath::vec3 pivotFocus = m_hasFocus ? m_focusWorld : defaultFocusOr(defaultFallback);
        return IRMath::orthographicPivotOffset(
            cameraIso,
            pivotFocus,
            IRMath::orthographicViewBasis(viewRotation)
        );
    }

    static IRMath::vec2 cameraIsoPreservingView(
        IRMath::vec2 effectiveCameraIso, IRMath::vec3 focusWorld, IRMath::vec4 viewRotation
    ) {
        return effectiveCameraIso - IRMath::orthographicPivotOffset(
                                        IRMath::vec2(0.0f),
                                        focusWorld,
                                        IRMath::orthographicViewBasis(viewRotation)
                                    );
    }

  private:
    IRMath::vec3 m_focusWorld = IRMath::vec3(0.0f);
    IRMath::vec3 m_defaultFocusWorld = IRMath::vec3(0.0f);
    bool m_hasFocus = false;
    bool m_hasDefault = false;
};

} // namespace IRRender::detail

#endif
