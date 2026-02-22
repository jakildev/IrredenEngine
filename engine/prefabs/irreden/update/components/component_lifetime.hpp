#ifndef COMPONENT_LIFETIME_H
#define COMPONENT_LIFETIME_H

namespace IRComponents {

struct C_Lifetime {
    int life_;

    C_Lifetime(int life)
        : life_(life) {}

    // Default
    C_Lifetime()
        : life_(1) {}
};

} // namespace IRComponents

#endif /* COMPONENT_LIFETIME_H */
