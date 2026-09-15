#ifndef IR_EDT_H
#define IR_EDT_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace IRMath {

constexpr std::int64_t kSquaredEdtInfinity = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kSquaredEdtMaxFiniteCost =
    static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max() - 1) *
    (std::numeric_limits<std::int32_t>::max() - 1);

struct SquaredEdtScratch {
    std::vector<std::int32_t> envelope_;
    std::vector<std::int64_t> starts_;
};

namespace detail {

inline void validateSquaredEdtSize(std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("squared EDT span is too large");
    }
}

constexpr std::int64_t lowerEnvelopeStart(std::int64_t numerator, std::int64_t denominator) {
    const std::int64_t quotient = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    return quotient - static_cast<std::int64_t>(remainder < 0) + 1;
}

inline bool spansOverlap(std::span<const std::int64_t> input, std::span<std::int64_t> output) {
    const auto inputBegin = reinterpret_cast<std::uintptr_t>(input.data());
    const auto inputEnd = inputBegin + input.size_bytes();
    const auto outputBegin = reinterpret_cast<std::uintptr_t>(output.data());
    const auto outputEnd = outputBegin + output.size_bytes();
    return inputBegin < outputEnd && outputBegin < inputEnd;
}

} // namespace detail

inline void squaredDistanceTransform1D(
    std::span<const std::int64_t> input, std::span<std::int64_t> output, SquaredEdtScratch &scratch
) {
    if (input.size() != output.size()) {
        throw std::invalid_argument("squared EDT spans must have equal sizes");
    }
    detail::validateSquaredEdtSize(input.size());
    if (input.empty()) {
        return;
    }
    if (detail::spansOverlap(input, output)) {
        throw std::invalid_argument("squared EDT spans must not overlap");
    }

    for (std::int64_t cost : input) {
        if (cost != kSquaredEdtInfinity && (cost < 0 || cost > kSquaredEdtMaxFiniteCost)) {
            throw std::invalid_argument("squared EDT cost is outside the supported domain");
        }
    }

    scratch.envelope_.resize(input.size());
    scratch.starts_.resize(input.size());
    std::int32_t envelopeSize = 0;
    for (std::int32_t q = 0; q < static_cast<std::int32_t>(input.size()); ++q) {
        if (input[q] == kSquaredEdtInfinity) {
            continue;
        }

        std::int64_t start = std::numeric_limits<std::int64_t>::min();
        while (envelopeSize > 0) {
            const std::int32_t v = scratch.envelope_[envelopeSize - 1];
            const std::int64_t q64 = q;
            const std::int64_t v64 = v;
            const std::int64_t numerator = (input[q] + q64 * q64) - (input[v] + v64 * v64);
            start = detail::lowerEnvelopeStart(numerator, 2 * (q64 - v64));
            if (start > scratch.starts_[envelopeSize - 1]) {
                break;
            }
            --envelopeSize;
        }

        scratch.envelope_[envelopeSize] = q;
        scratch.starts_[envelopeSize] =
            envelopeSize == 0 ? std::numeric_limits<std::int64_t>::min() : start;
        ++envelopeSize;
    }

    if (envelopeSize == 0) {
        std::fill(output.begin(), output.end(), kSquaredEdtInfinity);
        return;
    }

    std::int32_t envelopeIndex = 0;
    for (std::int32_t p = 0; p < static_cast<std::int32_t>(output.size()); ++p) {
        while (envelopeIndex + 1 < envelopeSize && scratch.starts_[envelopeIndex + 1] <= p) {
            ++envelopeIndex;
        }
        const std::int64_t delta = static_cast<std::int64_t>(p) - scratch.envelope_[envelopeIndex];
        output[p] = input[scratch.envelope_[envelopeIndex]] + delta * delta;
    }
}

} // namespace IRMath

#endif /* IR_EDT_H */
