#ifndef COMPONENT_VELOCITY_DRAG_H
#define COMPONENT_VELOCITY_DRAG_H

namespace IRComponents {

struct C_VelocityDrag {
    float dragPerSecond_;
    float driftDelaySeconds_;
    float driftUpAccelPerSecond_;
    float minSpeed_;
    float elapsedSeconds_;

    C_VelocityDrag(
        float dragPerSecond,
        float driftDelaySeconds,
        float driftUpAccelPerSecond,
        float minSpeed = 0.01f
    )
        : dragPerSecond_{dragPerSecond}
        , driftDelaySeconds_{driftDelaySeconds}
        , driftUpAccelPerSecond_{driftUpAccelPerSecond}
        , minSpeed_{minSpeed}
        , elapsedSeconds_{0.0f} {}

    C_VelocityDrag()
        : C_VelocityDrag(1.8f, 0.12f, 5.0f, 0.01f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_VELOCITY_DRAG_H */
