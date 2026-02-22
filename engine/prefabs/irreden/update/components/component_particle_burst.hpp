#ifndef COMPONENT_PARTICLE_BURST_H
#define COMPONENT_PARTICLE_BURST_H

namespace IRComponents {

struct C_ParticleBurst {
    int count_;
    int lifetime_;
    float speed_;

    C_ParticleBurst(int count, int lifetime, float speed)
        : count_{count}, lifetime_{lifetime}, speed_{speed} {}

    C_ParticleBurst() : C_ParticleBurst(6, 40, 12.0f) {}
};

} // namespace IRComponents

#endif /* COMPONENT_PARTICLE_BURST_H */
