#ifndef LOD_ZOOM_PROFILE_H
#define LOD_ZOOM_PROFILE_H

// The zoom -> LOD tier mapping a camera selects with, and the per-consumer
// history a hysteresis deadband needs. Pure value types: LOD_UPDATE and
// SYNC_VIEWPORT_SUBJECTS both resolve through them, so the main camera and a
// secondary viewport cross a threshold by the same rule. Policy, units and
// inheritance: docs/design/lod-strategy.md §"Phase 1d".

#include <irreden/ir_math.hpp>
#include <irreden/render/lod_level.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace IRRender {

inline constexpr std::size_t kLodZoomThresholdCount = 4;

// Four ascending display-zoom thresholds (the `C_ZoomLevel` scale, max of x
// and y) and a hysteresis half-width in octaves. `thresholds()[i]` is the
// boundary between tier LOD_(4 - i) and the finer LOD_(3 - i). Every value
// that exists is valid: the only constructors are the default profile and
// `make`, which rejects what `whyInvalid` names.
class LodZoomProfile {
  public:
    using Thresholds = std::array<float, kLodZoomThresholdCount>;

    static constexpr Thresholds kDefaultThresholds{2.0f, 4.0f, 8.0f, 16.0f};

    LodZoomProfile() = default;

    // Null when the profile is valid, else the first rule it breaks: every
    // threshold finite and positive, strictly ascending; the half-width finite
    // and nonnegative; and no two transition bands
    // [t / 2^h, t * 2^h] overlapping (touching is allowed).
    static std::optional<std::string_view>
    whyInvalid(const Thresholds &thresholds, float hysteresisOctaves) {
        for (std::size_t i = 0; i < kLodZoomThresholdCount; ++i) {
            if (!IRMath::isFinite(thresholds[i]) || !(thresholds[i] > 0.0f)) {
                return "LOD zoom thresholds must be finite and positive";
            }
            if (i > 0 && !(thresholds[i] > thresholds[i - 1])) {
                return "LOD zoom thresholds must be strictly ascending";
            }
        }
        if (!IRMath::isFinite(hysteresisOctaves) || hysteresisOctaves < 0.0f) {
            return "LOD zoom hysteresis must be finite and nonnegative";
        }
        const float factor = IRMath::pow(2.0f, hysteresisOctaves);
        for (std::size_t i = 0; i + 1 < kLodZoomThresholdCount; ++i) {
            if (!(thresholds[i] * factor <= thresholds[i + 1] / factor)) {
                return "LOD zoom hysteresis bands must not overlap";
            }
        }
        return std::nullopt;
    }

    static std::optional<LodZoomProfile>
    make(const Thresholds &thresholds, float hysteresisOctaves = 0.0f) {
        if (whyInvalid(thresholds, hysteresisOctaves)) {
            return std::nullopt;
        }
        LodZoomProfile profile;
        profile.m_thresholds = thresholds;
        profile.m_hysteresisOctaves = hysteresisOctaves;
        profile.m_factor = IRMath::pow(2.0f, hysteresisOctaves);
        return profile;
    }

    const Thresholds &thresholds() const {
        return m_thresholds;
    }

    float hysteresisOctaves() const {
        return m_hysteresisOctaves;
    }

    // The tier with no history: the finer side owns each exact threshold, so
    // the default profile is 1..2 -> LOD_4, 2..4 -> LOD_3, ..., >= 16 ->
    // LOD_0. Initializes every consumer; a nonfinite zoom reads as LOD_4.
    LodLevel tierAt(float zoom) const {
        auto tier = static_cast<std::uint32_t>(LodLevel::LOD_4);
        for (const float threshold : m_thresholds) {
            tier -= zoom >= threshold ? 1u : 0u;
        }
        return static_cast<LodLevel>(tier);
    }

    // The tier after @p previous sees @p zoom. Moving finer crosses a
    // threshold at `t * 2^h` or above; moving coarser crosses strictly below
    // `t / 2^h`; inside a band the tier holds. A jump resolves every crossed
    // threshold at once. At width zero this is `tierAt` from any @p previous.
    // A nonfinite zoom keeps @p previous.
    LodLevel advance(LodLevel previous, float zoom) const {
        if (!IRMath::isFinite(zoom)) {
            return previous;
        }
        constexpr auto kCoarsest = static_cast<std::uint32_t>(LodLevel::LOD_4);
        auto tier = static_cast<std::uint32_t>(previous);
        while (tier > 0 && zoom >= m_thresholds[kCoarsest - tier] * m_factor) {
            --tier;
        }
        while (tier < kCoarsest && zoom < m_thresholds[kCoarsest - 1 - tier] / m_factor) {
            ++tier;
        }
        return static_cast<LodLevel>(tier);
    }

    bool operator==(const LodZoomProfile &) const = default;

  private:
    Thresholds m_thresholds = kDefaultThresholds;
    float m_hysteresisOctaves = 0.0f;
    // 2^m_hysteresisOctaves, fixed by `make`.
    float m_factor = 1.0f;
};

// One consumer's previous tier. Unseeded until the first `advance`, which
// takes `tierAt(zoom)`; `reset` returns it there. The history belongs to the
// profile it was resolved under: an `advance` with any other profile (a
// replacement, a removal back to the default, a viewport switching between
// inherited and own) re-seeds from the current zoom rather than carrying a
// tier the new deadbands never produced.
struct LodTierHistory {
    LodLevel tier_ = LodLevel::LOD_4;
    LodZoomProfile profile_{};
    bool seeded_ = false;

    LodLevel advance(const LodZoomProfile &profile, float zoom) {
        if (!seeded_ || !(profile == profile_)) {
            profile_ = profile;
            tier_ = profile.tierAt(zoom);
            seeded_ = true;
            return tier_;
        }
        tier_ = profile.advance(tier_, zoom);
        return tier_;
    }

    void reset() {
        seeded_ = false;
    }
};

} // namespace IRRender

#endif /* LOD_ZOOM_PROFILE_H */
