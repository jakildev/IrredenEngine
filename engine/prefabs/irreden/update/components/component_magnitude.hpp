#ifndef COMPONENT_MAGNITUDE_H
#define COMPONENT_MAGNITUDE_H

namespace IRComponents {

struct C_Magnitude {
    float magnitude_;

    C_Magnitude(float value)
        : magnitude_{abs(value)} {}

    C_Magnitude()
        : C_Magnitude(1.0f) {}

    void set(float value) {
        magnitude_ = abs(value);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_MAGNITUDE_H */
