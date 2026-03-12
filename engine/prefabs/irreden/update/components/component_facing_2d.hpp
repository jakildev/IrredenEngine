#ifndef COMPONENT_FACING_2D_H
#define COMPONENT_FACING_2D_H

namespace IRComponents {

struct C_Facing2D {
    float angle_;     // radians, 0 = +X direction, CCW
    float turnRate_;  // radians per second (~6 = ~1 full turn/sec)

    C_Facing2D()
        : angle_{0.0f}
        , turnRate_{6.0f} {}

    explicit C_Facing2D(float turnRate)
        : angle_{0.0f}
        , turnRate_{turnRate} {}

    C_Facing2D(float angle, float turnRate)
        : angle_{angle}
        , turnRate_{turnRate} {}
};

} // namespace IRComponents

#endif /* COMPONENT_FACING_2D_H */
