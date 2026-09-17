#ifndef IR_RNG_PCG32_H
#define IR_RNG_PCG32_H

// PURPOSE: Explicitly seeded PCG-XSH-RR 64/32 generator for sampling that
//   must be byte-identical across platforms and threads. No thread-local
//   state, and callers map raw words to ranges through uniformBelow rather
//   than a standard distribution. See docs/design/chunked-field-placement-kit.md (D7).

#include <cstdint>

namespace IRMath {

class Pcg32 {
  public:
    // O'Neill's reference seeding: the stream selects the odd increment, so
    // (seed, stream) pairs reproduce the published pcg32 demo output.
    explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 0) noexcept
        : m_inc((stream << 1u) | 1u) {
        step();
        m_state += seed;
        step();
    }

    std::uint32_t nextWord() noexcept {
        const std::uint64_t old = m_state;
        step();
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

  private:
    std::uint64_t m_state = 0;
    std::uint64_t m_inc;

    void step() noexcept {
        m_state = m_state * 6364136223846793005ULL + m_inc;
    }
};

// One word in, a value in [0, n) out, never rejecting, so the stream position
// after a call depends only on the call count. Requires n >= 1. The map is
// multiply-shift, not `%`: both are portable, but they are different maps and
// yield different sequences from the same words.
inline std::uint32_t uniformBelow(Pcg32 &rng, std::uint32_t n) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(rng.nextWord()) * n) >> 32u);
}

} // namespace IRMath

#endif /* IR_RNG_PCG32_H */
