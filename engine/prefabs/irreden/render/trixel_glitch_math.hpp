#ifndef TRIXEL_GLITCH_MATH_H
#define TRIXEL_GLITCH_MATH_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_glitch_subject.hpp>
#include <irreden/render/glitch_math.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The deterministic contract of the canvas-space subject glitch
// (`TRIXEL_GLITCH`): the per-subject choices the CPU evaluates once per period
// tick, the GPU subject buffer layout and its id -> slot table, the
// closest-two influence rule, pair interference, and a per-texel CPU twin of
// `c_trixel_glitch.{glsl,metal}`. Effect ids continue the framebuffer pass's
// id space; constants shared with the kernels live in `ir_glitch_common` and
// carry the same values. Every spatial offset is a whole canvas trixel and
// every colour operation is integer arithmetic on 8-bit channels, so the two
// backends and this twin agree bit for bit.
namespace IRPrefab::Glitch {

inline constexpr std::uint32_t kEffectSubjectTear = 6u;
inline constexpr std::uint32_t kEffectSubjectHold = 7u;
inline constexpr std::uint32_t kEffectSubjectDissolve = 8u;
inline constexpr std::uint32_t kEffectSubjectPair = 9u;
inline constexpr std::uint32_t kEffectSubjectFrame = 10u;

// Lanes of `kEffectSubjectFrame`, xor'd with the subject's salt.
inline constexpr std::uint32_t kLaneSubjectBandRows = 0u;
inline constexpr std::uint32_t kLaneSubjectSplitSign = 1u;
inline constexpr std::uint32_t kLaneSubjectEchoA = 2u;
inline constexpr std::uint32_t kLaneSubjectEchoB = 3u;
inline constexpr std::uint32_t kLaneSubjectTint = 4u;
inline constexpr std::uint32_t kLaneSubjectDissolveCell = 5u;

inline constexpr std::uint32_t kSubjectSaltTag = 0x5B7E1D03u;
inline constexpr std::uint32_t kSubjectTableTag = 0x2C1B3C6Du;

inline constexpr int kSubjectMaxSpill = 4;
// The kernel stages ids within this radius of its 16x16 group: spill reaches
// `kSubjectMaxSpill`, and the echo edge test reads one texel beyond an echo
// offset of up to `kSubjectMaxSpill`.
inline constexpr int kSubjectHalo = kSubjectMaxSpill + 1;
inline constexpr int kTrixelGlitchGroupSize = 16;
inline constexpr int kSubjectTileSize = kTrixelGlitchGroupSize + 2 * kSubjectHalo;
inline constexpr int kSubjectMaxTearTrixels = 8;
inline constexpr int kSubjectMaxSplitTrixels = 2;
inline constexpr int kSubjectBandRowsSpan = 4;
inline constexpr int kSubjectDissolveCellSpan = 3;
inline constexpr int kSubjectTintPaletteSize = 4;
// Encoded distance of a texel no geometry wrote.
inline constexpr int kSubjectEmptyDistance = IRConstants::kTrixelDistanceMaxDistance;

// GPU buffer layout, in uvec4 words: a three-word header, the open-addressed
// table (one word per entry), `kSubjectRecordWords` words per subject, then the
// per-axis frame tail.
inline constexpr std::uint32_t kSubjectHeaderWords = 3u;
inline constexpr std::uint32_t kSubjectRecordWords = 5u;
inline constexpr std::uint32_t kPerAxisFrameWords = 2u;
inline constexpr int kPerAxisFaceStep = 2;
inline constexpr int kPerAxisSubjectTileSize = 16;
inline constexpr int kPerAxisSubjectTileHalo = kPerAxisFaceStep * kSubjectHalo;
inline constexpr int kPerAxisSubjectTileWindow =
    kPerAxisSubjectTileSize + 2 * kPerAxisSubjectTileHalo;

static_assert(kSubjectTileSize == 26, "the kernels declare a 26x26 id tile");
static_assert(kPerAxisSubjectTileWindow == 36, "the per-axis resolve stages a 36x36 window");

inline IRMath::ivec4
perAxisSubjectTileBounds(IRMath::ivec2 subjectCell, int spill, IRMath::ivec2 storeSize) {
    IR_ASSERT(spill >= 0 && spill <= kSubjectMaxSpill, "per-axis tile mark spill is invalid");
    IR_ASSERT(storeSize.x > 0 && storeSize.y > 0, "per-axis tile mark needs a store");
    const int reach = kPerAxisFaceStep * spill;
    const IRMath::ivec2 firstCell = IRMath::max(subjectCell - reach, IRMath::ivec2(0));
    const IRMath::ivec2 lastCell = IRMath::min(subjectCell + reach, storeSize - IRMath::ivec2(1));
    return IRMath::ivec4(
        firstCell.x / kPerAxisSubjectTileSize,
        firstCell.y / kPerAxisSubjectTileSize,
        lastCell.x / kPerAxisSubjectTileSize,
        lastCell.y / kPerAxisSubjectTileSize
    );
}

inline bool isSubjectActive(const IRComponents::C_GlitchSubject &subject) {
    return subject.weight_ > 0.0f && (subject.tearWeight_ > 0.0f || subject.holdWeight_ > 0.0f ||
                                      subject.splitWeight_ > 0.0f || subject.echoWeight_ > 0.0f ||
                                      subject.dissolveWeight_ > 0.0f);
}

inline bool subjectReadsHistory(const IRComponents::C_GlitchSubject &subject) {
    return isSubjectActive(subject) && subject.holdWeight_ > 0.0f;
}

constexpr std::uint32_t entityIdLow(std::uint64_t id) {
    return static_cast<std::uint32_t>(id);
}

constexpr std::uint32_t entityIdHigh(std::uint64_t id) {
    return static_cast<std::uint32_t>(id >> 32);
}

constexpr std::uint32_t subjectSalt(std::uint64_t id) {
    return hash3(entityIdLow(id), entityIdHigh(id), kSubjectSaltTag);
}

// Additive tints the echo copies take.
inline IRMath::uvec4 subjectTint(int index) {
    constexpr std::array<IRMath::uvec4, kSubjectTintPaletteSize> kPalette{{
        {255u, 64u, 214u, 255u},
        {64u, 240u, 255u, 255u},
        {255u, 230u, 64u, 255u},
        {120u, 255u, 90u, 255u},
    }};
    return kPalette[static_cast<std::size_t>(index % kSubjectTintPaletteSize)];
}

// One subject's choices for one period tick, every one evaluated on the CPU
// through the hash ABI and uploaded; the kernel hashes only per band row and
// per dissolve cell.
struct SubjectRecord {
    std::uint64_t id_ = 0;
    std::uint32_t salt_ = 0u;
    int spill_ = 0;
    bool depthEnabled_ = false;
    int depthNear_ = 0;
    int depthFar_ = 0;
    std::uint32_t tearThreshold_ = 0u;
    std::uint32_t holdThreshold_ = 0u;
    std::uint32_t dissolveThreshold_ = 0u;
    // Echo blend toward the tint, in 1/256ths.
    int echoStrength_ = 0;
    int tearMaxTrixels_ = 0;
    // Signed x offset of the red read; blue reads the opposite side. 0 is off.
    int splitTrixels_ = 0;
    int bandRows_ = 1;
    int dissolveCell_ = 1;
    IRMath::ivec2 echoA_ = IRMath::ivec2(0);
    IRMath::ivec2 echoB_ = IRMath::ivec2(0);
    int tintA_ = 0;
    int tintB_ = 0;
};

inline SubjectRecord subjectRecord(
    std::uint64_t id,
    const IRComponents::C_GlitchSubject &subject,
    std::uint32_t seed,
    TickWords tick
) {
    SubjectRecord record;
    record.id_ = id;
    record.salt_ = subjectSalt(id);
    record.spill_ = subject.spill_;
    record.depthEnabled_ = subject.depthEnabled_;
    record.depthNear_ = subject.depthNear_;
    record.depthFar_ = subject.depthFar_;
    const auto frameLane = [&](std::uint32_t lane) {
        return glitchHash(seed, tick, kEffectSubjectFrame, record.salt_ ^ lane);
    };
    record.bandRows_ =
        1 + static_cast<int>(
                frameLane(kLaneSubjectBandRows) % static_cast<std::uint32_t>(kSubjectBandRowsSpan)
            );
    record.dissolveCell_ = 1 + static_cast<int>(
                                   frameLane(kLaneSubjectDissolveCell) %
                                   static_cast<std::uint32_t>(kSubjectDissolveCellSpan)
                               );

    const float tear = subject.weight_ * subject.tearWeight_;
    record.tearThreshold_ = hashThreshold(tear);
    record.tearMaxTrixels_ = tear > 0.0f ? maxOffsetTrixels(tear, kSubjectMaxTearTrixels) : 0;
    record.holdThreshold_ = hashThreshold(subject.weight_ * subject.holdWeight_);
    record.dissolveThreshold_ = hashThreshold(subject.weight_ * subject.dissolveWeight_);

    const float split = subject.weight_ * subject.splitWeight_;
    if (split > 0.0f) {
        const int sign = (frameLane(kLaneSubjectSplitSign) & 1u) != 0u ? 1 : -1;
        record.splitTrixels_ = sign * maxOffsetTrixels(split, kSubjectMaxSplitTrixels);
    }

    record.echoStrength_ = static_cast<int>(256.0f * subject.weight_ * subject.echoWeight_);
    const int reach = IRMath::max(1, IRMath::min(subject.spill_, kSubjectMaxSpill));
    const std::uint32_t directionA = frameLane(kLaneSubjectEchoA) % 8u;
    const std::uint32_t directionB = (directionA + 1u + frameLane(kLaneSubjectEchoB) % 7u) % 8u;
    record.echoA_ = cellNeighbour(directionA) * reach;
    record.echoB_ = cellNeighbour(directionB) * IRMath::max(1, (reach + 1) / 2);
    const std::uint32_t tint = frameLane(kLaneSubjectTint);
    record.tintA_ = static_cast<int>(tint % static_cast<std::uint32_t>(kSubjectTintPaletteSize));
    record.tintB_ =
        (record.tintA_ + 1 + static_cast<int>((tint >> 8) % 3u)) % kSubjectTintPaletteSize;
    return record;
}

// ---- GPU buffer: header, id -> slot table, records -------------------------

// Power-of-two capacity at or below half load.
inline std::uint32_t subjectTableCapacity(std::size_t subjectCount) {
    return IRMath::max(4u, IRMath::nextPowerOfTwo(2u * static_cast<std::uint32_t>(subjectCount)));
}

constexpr std::uint32_t subjectTableHome(std::uint64_t id, std::uint32_t mask) {
    return hash3(entityIdLow(id), entityIdHigh(id), kSubjectTableTag) & mask;
}

inline IRMath::uvec4 packSigned(int x, int y, int z, int w) {
    return IRMath::uvec4(
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(z),
        static_cast<std::uint32_t>(w)
    );
}

// Header word 1: table mask, table base word, record base word, max spill.
struct SubjectBufferLayout {
    std::uint32_t tableMask_ = 0u;
    std::uint32_t tableBase_ = kSubjectHeaderWords;
    std::uint32_t recordBase_ = kSubjectHeaderWords;
    std::uint32_t perAxisFrameBase_ = kSubjectHeaderWords;
    std::uint32_t wordCount_ = kSubjectHeaderWords;
};

inline SubjectBufferLayout subjectBufferLayout(std::size_t subjectCount) {
    SubjectBufferLayout layout;
    const std::uint32_t capacity = subjectTableCapacity(subjectCount);
    layout.tableMask_ = capacity - 1u;
    layout.recordBase_ = layout.tableBase_ + capacity;
    layout.perAxisFrameBase_ =
        layout.recordBase_ + kSubjectRecordWords * static_cast<std::uint32_t>(subjectCount);
    layout.wordCount_ = layout.perAxisFrameBase_ + kPerAxisFrameWords;
    return layout;
}

// Writes the whole buffer for @p records, which must be sorted by ascending
// id with no duplicate and no null id; a record's index is its slot.
inline void packSubjectWords(
    std::span<const SubjectRecord> records,
    std::uint32_t seed,
    TickWords tick,
    std::vector<IRMath::uvec4> &out
) {
    const SubjectBufferLayout layout = subjectBufferLayout(records.size());
    out.assign(layout.wordCount_, IRMath::uvec4(0u));
    int maxSpill = 0;
    for (const SubjectRecord &record : records) {
        maxSpill = IRMath::max(maxSpill, record.spill_);
    }
    out[0] = IRMath::uvec4(seed, tick.low_, tick.high_, static_cast<std::uint32_t>(records.size()));
    out[1] = IRMath::uvec4(
        layout.tableMask_,
        layout.tableBase_,
        layout.recordBase_,
        static_cast<std::uint32_t>(maxSpill)
    );
    out[2] = IRMath::uvec4(layout.perAxisFrameBase_, 0u, 0u, 0u);
    for (std::size_t slot = 0; slot < records.size(); ++slot) {
        const SubjectRecord &record = records[slot];
        std::uint32_t entry = subjectTableHome(record.id_, layout.tableMask_);
        while (out[layout.tableBase_ + entry].x != 0u || out[layout.tableBase_ + entry].y != 0u) {
            entry = (entry + 1u) & layout.tableMask_;
        }
        out[layout.tableBase_ + entry] = IRMath::uvec4(
            entityIdLow(record.id_),
            entityIdHigh(record.id_),
            static_cast<std::uint32_t>(slot),
            0u
        );
        const std::uint32_t base =
            layout.recordBase_ + kSubjectRecordWords * static_cast<std::uint32_t>(slot);
        out[base + 0u] = IRMath::uvec4(
            entityIdLow(record.id_),
            entityIdHigh(record.id_),
            record.salt_,
            static_cast<std::uint32_t>(record.spill_) | (record.depthEnabled_ ? 0x100u : 0u)
        );
        out[base + 1u] = IRMath::uvec4(
            record.tearThreshold_,
            record.holdThreshold_,
            record.dissolveThreshold_,
            static_cast<std::uint32_t>(record.echoStrength_)
        );
        out[base + 2u] = packSigned(
            record.tearMaxTrixels_,
            record.splitTrixels_,
            record.bandRows_,
            record.dissolveCell_
        );
        out[base + 3u] =
            packSigned(record.echoA_.x, record.echoA_.y, record.echoB_.x, record.echoB_.y);
        out[base + 4u] =
            packSigned(record.depthNear_, record.depthFar_, record.tintA_, record.tintB_);
    }
}

inline void setPerAxisFrameWords(
    std::vector<IRMath::uvec4> &words,
    IRMath::ivec4 storeFrame,
    IRMath::ivec2 snapshotOrigin,
    int depthScale,
    int depthBias
) {
    const std::uint32_t base = words[2].x;
    words[base] = packSigned(storeFrame.x, storeFrame.y, storeFrame.z, kPerAxisFaceStep);
    words[base + 1u] = packSigned(snapshotOrigin.x, snapshotOrigin.y, depthScale, depthBias);
}

// The kernel's id -> slot probe: -1 for the null id or an id with no subject.
inline int lookupSubjectSlot(std::span<const IRMath::uvec4> words, std::uint64_t id) {
    if (id == 0u) {
        return -1;
    }
    const std::uint32_t mask = words[1].x;
    const std::uint32_t tableBase = words[1].y;
    std::uint32_t entry = subjectTableHome(id, mask);
    for (std::uint32_t probe = 0u; probe <= mask; ++probe) {
        const IRMath::uvec4 &word = words[tableBase + entry];
        if (word.x == 0u && word.y == 0u) {
            return -1;
        }
        if (word.x == entityIdLow(id) && word.y == entityIdHigh(id)) {
            return static_cast<int>(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

// Reads slot @p slot back out of a packed buffer, the way the kernel does.
inline SubjectRecord unpackSubjectRecord(std::span<const IRMath::uvec4> words, int slot) {
    const std::uint32_t base = words[1].z + kSubjectRecordWords * static_cast<std::uint32_t>(slot);
    const IRMath::uvec4 w0 = words[base + 0u];
    const IRMath::uvec4 w1 = words[base + 1u];
    const IRMath::uvec4 w2 = words[base + 2u];
    const IRMath::uvec4 w3 = words[base + 3u];
    const IRMath::uvec4 w4 = words[base + 4u];
    SubjectRecord record;
    record.id_ = static_cast<std::uint64_t>(w0.x) | (static_cast<std::uint64_t>(w0.y) << 32);
    record.salt_ = w0.z;
    record.spill_ = static_cast<int>(w0.w & 0xFFu);
    record.depthEnabled_ = (w0.w & 0x100u) != 0u;
    record.tearThreshold_ = w1.x;
    record.holdThreshold_ = w1.y;
    record.dissolveThreshold_ = w1.z;
    record.echoStrength_ = static_cast<int>(w1.w);
    record.tearMaxTrixels_ = static_cast<int>(w2.x);
    record.splitTrixels_ = static_cast<int>(w2.y);
    record.bandRows_ = static_cast<int>(w2.z);
    record.dissolveCell_ = static_cast<int>(w2.w);
    record.echoA_ = IRMath::ivec2(static_cast<int>(w3.x), static_cast<int>(w3.y));
    record.echoB_ = IRMath::ivec2(static_cast<int>(w3.z), static_cast<int>(w3.w));
    record.depthNear_ = static_cast<int>(w4.x);
    record.depthFar_ = static_cast<int>(w4.y);
    record.tintA_ = static_cast<int>(w4.z);
    record.tintB_ = static_cast<int>(w4.w);
    return record;
}

// ---- Influence: the closest two subjects whose spill reaches a texel -------

struct Influence {
    int count_ = 0;
    int slotA_ = -1;
    int distA_ = 0;
    int slotB_ = -1;
    int distB_ = 0;
};

// Nearer first; ties by ascending slot, which is ascending entity id.
constexpr bool influenceBefore(int slot, int dist, int otherSlot, int otherDist) {
    return dist < otherDist || (dist == otherDist && slot < otherSlot);
}

// The candidate scan the kernel runs, in the same row-major order. A subject
// reaches texel `p` at Chebyshev distance `d` from one of its silhouette
// texels when `d <= spill`; the two nearest distinct subjects win. Every
// subject texel within @p radius of @p p is visited, so the result does not
// depend on the scan order.
template <typename SlotAt, typename SpillOf>
Influence
resolveInfluenceStepped(IRMath::ivec2 p, int radius, int step, SlotAt &&slotAt, SpillOf &&spillOf) {
    Influence influence;
    const int scanRadius = radius * step;
    for (int dy = -scanRadius; dy <= scanRadius; ++dy) {
        for (int dx = -scanRadius; dx <= scanRadius; ++dx) {
            if (IRMath::floorMod(static_cast<std::int64_t>(dx + dy), step) != 0) {
                continue;
            }
            const int slot = slotAt(p + IRMath::ivec2(dx, dy));
            if (slot < 0) {
                continue;
            }
            const int dist = IRMath::divCeil(IRMath::max(IRMath::abs(dx), IRMath::abs(dy)), step);
            if (dist > spillOf(slot)) {
                continue;
            }
            if (slot == influence.slotA_) {
                influence.distA_ = IRMath::min(influence.distA_, dist);
            } else if (slot == influence.slotB_) {
                influence.distB_ = IRMath::min(influence.distB_, dist);
            } else if (
                influence.slotB_ < 0 ||
                influenceBefore(slot, dist, influence.slotB_, influence.distB_)
            ) {
                influence.slotB_ = slot;
                influence.distB_ = dist;
            }
            if (influence.slotB_ >= 0 && (influence.slotA_ < 0 || influenceBefore(
                                                                      influence.slotB_,
                                                                      influence.distB_,
                                                                      influence.slotA_,
                                                                      influence.distA_
                                                                  ))) {
                const int slotA = influence.slotA_;
                const int distA = influence.distA_;
                influence.slotA_ = influence.slotB_;
                influence.distA_ = influence.distB_;
                influence.slotB_ = slotA;
                influence.distB_ = distA;
            }
        }
    }
    influence.count_ = influence.slotA_ < 0 ? 0 : (influence.slotB_ < 0 ? 1 : 2);
    return influence;
}

template <typename SlotAt, typename SpillOf>
Influence resolveInfluence(IRMath::ivec2 p, int radius, SlotAt &&slotAt, SpillOf &&spillOf) {
    return resolveInfluenceStepped(
        p,
        radius,
        1,
        std::forward<SlotAt>(slotAt),
        std::forward<SpillOf>(spillOf)
    );
}

enum class PairMode : std::uint32_t {
    // Each canvas row takes one of the two subjects' effects.
    ROWS = 0,
    // The two subjects' effects blend half and half.
    MIX = 1,
    // Each subject's tear and split read the other subject's trixels.
    CROSS = 2,
};

inline constexpr std::uint32_t kPairModeCount = 3u;

// @p lo and @p hi are the pair ordered by ascending id.
inline std::uint32_t
pairHash(std::uint32_t seed, TickWords tick, std::uint64_t lo, std::uint64_t hi) {
    return glitchHash(
        seed,
        tick,
        kEffectSubjectPair,
        hash3(hash3(entityIdLow(lo), entityIdHigh(lo), entityIdLow(hi)), entityIdHigh(hi), 0u)
    );
}

inline PairMode pairMode(std::uint32_t hash) {
    return static_cast<PairMode>(hash % kPairModeCount);
}

// ---- The per-texel CPU twin ------------------------------------------------

// rgb toward @p tint by @p strength / 256; a transparent texel becomes the
// tint at that alpha.
inline IRMath::uvec4 echoBlend(IRMath::uvec4 colour, IRMath::uvec4 tint, int strength) {
    const auto w = static_cast<std::uint32_t>(strength);
    if (colour.a == 0u) {
        return IRMath::uvec4(tint.r, tint.g, tint.b, (255u * w) >> 8);
    }
    const IRMath::uvec4 mixed = (tint * w + colour * (256u - w)) >> 8u;
    return IRMath::uvec4(mixed.r, mixed.g, mixed.b, colour.a);
}

inline IRMath::uvec4 halfBlend(IRMath::uvec4 a, IRMath::uvec4 b) {
    return (a + b) >> 1u;
}

// Provenance of one output texel, for the fixture's structural probes.
struct TexelTrace {
    int influences_ = 0;
    // Signed whole-trixel x offset of an accepted tear source, 0 for none.
    int tearDx_ = 0;
    int splitDx_ = 0;
    bool held_ = false;
    // A held texel whose history id was another entity; never set by
    // construction, counted so a probe can prove it.
    bool heldForeign_ = false;
    bool echoed_ = false;
    bool dissolved_ = false;
};

// Inputs the kernel reads for one frame. `Canvas` provides, for any texel
// (out-of-range texels are never passed):
//   IRMath::ivec2 size(); IRMath::uvec4 colour(q); std::uint64_t id(q)
//   (carrier bits already stripped); int distance(q);
//   IRMath::uvec4 historyColour(q); std::uint64_t historyId(q).
template <typename Canvas> struct SubjectFrame {
    const Canvas &canvas_;
    std::span<const SubjectRecord> records_;
    std::uint32_t seed_ = 0u;
    TickWords tick_{};
    int maxSpill_ = 0;
    // id -> slot, -1 when the id has no subject.
    std::span<const IRMath::uvec4> words_;
    int spatialStep_ = 1;
    IRMath::ivec2 storeOrigin_{0};
    IRMath::ivec2 historyOrigin_{0};
    int depthScale_ = 1;
    int depthBias_ = 0;

    bool inBounds(IRMath::ivec2 q) const {
        const IRMath::ivec2 size = canvas_.size();
        return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
    }

    int slotAt(IRMath::ivec2 q) const {
        return inBounds(q) ? lookupSubjectSlot(words_, canvas_.id(q)) : -1;
    }

    bool isEdge(IRMath::ivec2 q, int slot) const {
        if (slotAt(q) != slot) {
            return false;
        }
        constexpr std::array<IRMath::ivec2, 4> kNeighbours{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const IRMath::ivec2 neighbour : kNeighbours) {
            if (slotAt(q + neighbour * spatialStep_) != slot) {
                return true;
            }
        }
        return false;
    }

    // Slot @p slot's effects at @p p; tear and split accept only trixels of
    // @p accept, which is the subject itself except in a CROSS pair.
    IRMath::uvec4
    applySubject(IRMath::ivec2 p, int slot, std::uint64_t accept, TexelTrace &trace) const {
        const SubjectRecord &record = records_[static_cast<std::size_t>(slot)];
        const std::uint64_t current = canvas_.id(p);
        const int distance = canvas_.distance(p);
        const int emptyDistance = spatialStep_ == 1 ? kSubjectEmptyDistance : 0x7FFFFFFF;
        const int decodedDepth =
            (distance >> (spatialStep_ == 1 ? 3 : 15)) * depthScale_ + depthBias_;
        const bool depthPasses = !record.depthEnabled_ ||
                                 (distance < emptyDistance && decodedDepth >= record.depthNear_ &&
                                  decodedDepth <= record.depthFar_);
        const IRMath::ivec2 relative = p - storeOrigin_;
        const int band =
            static_cast<int>(IRMath::floorDiv(relative.y, record.bandRows_ * spatialStep_));
        const std::uint32_t bandLane =
            record.salt_ ^ (static_cast<std::uint32_t>(band) * kCellHashY);

        IRMath::ivec2 source = p;
        if (record.tearThreshold_ > 0u && depthPasses) {
            const std::uint32_t hash = glitchHash(seed_, tick_, kEffectSubjectTear, bandLane);
            if ((hash & 0xFFFFu) < record.tearThreshold_) {
                const IRMath::ivec2 shifted =
                    p + IRMath::ivec2(
                            signedOffset(hash >> 16, record.tearMaxTrixels_) * spatialStep_,
                            0
                        );
                if (inBounds(shifted) && canvas_.id(shifted) == accept) {
                    source = shifted;
                    trace.tearDx_ = shifted.x - p.x;
                }
            }
        }
        IRMath::uvec4 colour = canvas_.colour(source);

        if (record.splitTrixels_ != 0) {
            const IRMath::ivec2 split(record.splitTrixels_ * spatialStep_, 0);
            const IRMath::ivec2 redAt = source + split;
            const IRMath::ivec2 blueAt = source - split;
            const bool redAccepted = inBounds(redAt) && canvas_.id(redAt) == accept;
            const bool blueAccepted = inBounds(blueAt) && canvas_.id(blueAt) == accept;
            const IRMath::uvec4 red = redAccepted ? canvas_.colour(redAt) : colour;
            const IRMath::uvec4 blue = blueAccepted ? canvas_.colour(blueAt) : colour;
            if (redAccepted || blueAccepted) {
                trace.splitDx_ = record.splitTrixels_ * spatialStep_;
            }
            colour = IRMath::uvec4(
                red.r,
                colour.g,
                blue.b,
                IRMath::max(IRMath::max(red.a, colour.a), blue.a)
            );
        }

        if (record.holdThreshold_ > 0u && depthPasses) {
            const std::uint32_t hash = glitchHash(seed_, tick_, kEffectSubjectHold, bandLane);
            if ((hash & 0xFFFFu) < record.holdThreshold_) {
                const IRMath::ivec2 historyAt = p - storeOrigin_ + historyOrigin_;
                const bool historyInBounds = inBounds(historyAt);
                const std::uint64_t held = historyInBounds ? canvas_.historyId(historyAt) : 0u;
                if (held == record.id_ || (current == record.id_ && held == 0u)) {
                    colour = historyInBounds ? canvas_.historyColour(historyAt) : IRMath::uvec4(0u);
                    trace.held_ = true;
                    trace.heldForeign_ = trace.heldForeign_ || (held != 0u && held != record.id_);
                }
            }
        }

        if (record.echoStrength_ > 0) {
            if (isEdge(p - record.echoA_ * spatialStep_, slot)) {
                colour = echoBlend(colour, subjectTint(record.tintA_), record.echoStrength_);
                trace.echoed_ = true;
            }
            if (isEdge(p - record.echoB_ * spatialStep_, slot)) {
                colour = echoBlend(colour, subjectTint(record.tintB_), record.echoStrength_);
                trace.echoed_ = true;
            }
        }

        if (record.dissolveThreshold_ > 0u && current == record.id_) {
            const int cellSize = record.dissolveCell_ * spatialStep_;
            const IRMath::ivec2 cell(
                IRMath::floorDiv(relative.x, cellSize),
                IRMath::floorDiv(relative.y, cellSize)
            );
            const std::uint32_t hash =
                glitchHash(seed_, tick_, kEffectSubjectDissolve, record.salt_ ^ cellLane(cell));
            if ((hash & 0xFFFFu) < record.dissolveThreshold_) {
                colour = IRMath::uvec4(0u);
                trace.dissolved_ = true;
            }
        }
        return colour;
    }

    IRMath::uvec4 texel(IRMath::ivec2 p, TexelTrace &trace) const {
        const IRMath::ivec2 relative = p - storeOrigin_;
        const Influence influence = resolveInfluenceStepped(
            p,
            maxSpill_,
            spatialStep_,
            [this](IRMath::ivec2 q) { return slotAt(q); },
            [this](int slot) { return records_[static_cast<std::size_t>(slot)].spill_; }
        );
        trace.influences_ = influence.count_;
        if (influence.count_ == 0) {
            return canvas_.colour(p);
        }
        const auto idOf = [this](int slot) { return records_[static_cast<std::size_t>(slot)].id_; };
        if (influence.count_ == 1) {
            return applySubject(p, influence.slotA_, idOf(influence.slotA_), trace);
        }
        const int lo = IRMath::min(influence.slotA_, influence.slotB_);
        const int hi = IRMath::max(influence.slotA_, influence.slotB_);
        const std::uint32_t hash = pairHash(seed_, tick_, idOf(lo), idOf(hi));
        switch (pairMode(hash)) {
        case PairMode::ROWS: {
            const int pick =
                (hash3(hash, static_cast<std::uint32_t>(relative.y), 0u) & 1u) != 0u ? hi : lo;
            return applySubject(p, pick, idOf(pick), trace);
        }
        case PairMode::MIX:
            return halfBlend(
                applySubject(p, lo, idOf(lo), trace),
                applySubject(p, hi, idOf(hi), trace)
            );
        case PairMode::CROSS:
        default:
            return halfBlend(
                applySubject(p, lo, idOf(hi), trace),
                applySubject(p, hi, idOf(lo), trace)
            );
        }
    }
};

} // namespace IRPrefab::Glitch

#endif /* TRIXEL_GLITCH_MATH_H */
