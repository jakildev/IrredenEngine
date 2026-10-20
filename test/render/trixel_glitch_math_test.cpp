// Shader-free contract of the canvas-space subject glitch (TRIXEL_GLITCH):
// subject validation and activity, the GPU buffer layout and its open-addressed
// id -> slot table, carrier-bit masking, the closest-two influence rule, the
// pair hash, and the per-texel CPU twin of c_trixel_glitch.{glsl,metal}. The
// fixed hash vectors were computed independently of this code from the
// documented algorithm.

#include <gtest/gtest.h>

#include <irreden/render/glitch_subject.hpp>
#include <irreden/render/trixel_glitch_math.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using IRComponents::C_GlitchSubject;
using IRMath::ivec2;
using IRMath::uvec4;
namespace Glitch = IRPrefab::Glitch;

C_GlitchSubject tearSubject(int spill = 0) {
    C_GlitchSubject subject;
    subject.tearWeight_ = 1.0f;
    subject.spill_ = spill;
    return subject;
}

std::vector<Glitch::SubjectRecord> records(
    const std::vector<std::uint64_t> &ids, const C_GlitchSubject &subject, std::uint32_t seed = 3u
) {
    std::vector<Glitch::SubjectRecord> out;
    for (const std::uint64_t id : ids) {
        out.push_back(Glitch::subjectRecord(id, subject, seed, Glitch::toTickWords(5)));
    }
    return out;
}

// ---- subject validation and activity ----------------------------------------

TEST(TrixelGlitchMathTest, DefaultSubjectIsValidButInactive) {
    const C_GlitchSubject subject;
    EXPECT_TRUE(Glitch::validateSubject(subject).empty());
    EXPECT_FLOAT_EQ(subject.weight_, 1.0f);
    EXPECT_EQ(subject.spill_, 0);
    EXPECT_FALSE(subject.depthEnabled_);
    EXPECT_FALSE(Glitch::isSubjectActive(subject)) << "no effect weight is set";

    C_GlitchSubject hold;
    hold.holdWeight_ = 0.5f;
    EXPECT_TRUE(Glitch::isSubjectActive(hold));
    EXPECT_TRUE(Glitch::subjectReadsHistory(hold));
    hold.weight_ = 0.0f;
    EXPECT_FALSE(Glitch::isSubjectActive(hold)) << "zero weight parks the subject";
    EXPECT_FALSE(Glitch::subjectReadsHistory(hold));
    EXPECT_FALSE(Glitch::subjectReadsHistory(tearSubject()));
}

TEST(TrixelGlitchMathTest, ValidateRejectsEachBrokenInvariant) {
    C_GlitchSubject subject = tearSubject();
    subject.weight_ = 1.5f;
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject = tearSubject();
    subject.echoWeight_ = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject = tearSubject();
    subject.dissolveWeight_ = -0.25f;
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject = tearSubject(Glitch::kSubjectMaxSpill + 1);
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject = tearSubject(-1);
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject = tearSubject();
    subject.depthEnabled_ = true;
    subject.depthNear_ = 4;
    subject.depthFar_ = 3;
    EXPECT_FALSE(Glitch::validateSubject(subject).empty());
    subject.depthFar_ = 4;
    EXPECT_TRUE(Glitch::validateSubject(subject).empty()) << "a one-step band is inclusive";
}

TEST(TrixelGlitchMathTest, SwappingAbsentGlitchImagesAsserts) {
    IRComponents::C_TriangleCanvasTextures canvas;
    EXPECT_FALSE(canvas.hasGlitchImages());
    EXPECT_THROW(canvas.swapGlitchScratchColors(), std::runtime_error);
}

// ---- buffer, table, lookup -------------------------------------------------------

TEST(TrixelGlitchMathTest, PackedBufferRoundTripsEverySubjectThroughTheTable) {
    C_GlitchSubject subject;
    subject.weight_ = 0.75f;
    subject.tearWeight_ = 1.0f;
    subject.holdWeight_ = 0.5f;
    subject.splitWeight_ = 0.25f;
    subject.echoWeight_ = 1.0f;
    subject.dissolveWeight_ = 0.5f;
    subject.spill_ = 3;
    subject.depthEnabled_ = true;
    subject.depthNear_ = -40;
    subject.depthFar_ = 12;
    const std::vector<std::uint64_t> ids{3u, 17u, 18u, 400u, 0x1'0000'0002ull};
    const auto source = records(ids, subject);
    std::vector<uvec4> words;
    Glitch::packSubjectWords(source, 3u, Glitch::toTickWords(5), words);

    const std::uint32_t capacity = words[1].x + 1u;
    EXPECT_EQ(capacity & (capacity - 1u), 0u) << "power-of-two table";
    EXPECT_GE(capacity, 2u * ids.size()) << "at or below half load";
    EXPECT_EQ(words[0], uvec4(3u, 5u, 0u, 5u));
    EXPECT_EQ(words[1].w, 3u) << "max spill";
    EXPECT_EQ(words.size(), Glitch::subjectBufferLayout(ids.size()).wordCount_);

    for (std::size_t slot = 0; slot < ids.size(); ++slot) {
        EXPECT_EQ(Glitch::lookupSubjectSlot(words, ids[slot]), static_cast<int>(slot));
        const Glitch::SubjectRecord back =
            Glitch::unpackSubjectRecord(words, static_cast<int>(slot));
        const Glitch::SubjectRecord &front = source[slot];
        EXPECT_EQ(back.id_, front.id_);
        EXPECT_EQ(back.salt_, front.salt_);
        EXPECT_EQ(back.spill_, front.spill_);
        EXPECT_EQ(back.depthEnabled_, front.depthEnabled_);
        EXPECT_EQ(back.depthNear_, -40);
        EXPECT_EQ(back.depthFar_, 12);
        EXPECT_EQ(back.tearThreshold_, front.tearThreshold_);
        EXPECT_EQ(back.holdThreshold_, front.holdThreshold_);
        EXPECT_EQ(back.dissolveThreshold_, front.dissolveThreshold_);
        EXPECT_EQ(back.echoStrength_, front.echoStrength_);
        EXPECT_EQ(back.tearMaxTrixels_, front.tearMaxTrixels_);
        EXPECT_EQ(back.splitTrixels_, front.splitTrixels_);
        EXPECT_EQ(back.bandRows_, front.bandRows_);
        EXPECT_EQ(back.dissolveCell_, front.dissolveCell_);
        EXPECT_EQ(back.echoA_, front.echoA_);
        EXPECT_EQ(back.echoB_, front.echoB_);
        EXPECT_EQ(back.tintA_, front.tintA_);
        EXPECT_EQ(back.tintB_, front.tintB_);
    }
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, 0u), -1) << "the null id is the empty sentinel";
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, 19u), -1);
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, 0x2'0000'0002ull), -1);
}

TEST(TrixelGlitchMathTest, CollidingIdsResolveByLinearProbing) {
    // Five ids that share one home entry in an 8-entry table (four subjects).
    std::vector<std::uint64_t> ids;
    const std::uint32_t mask = Glitch::subjectTableCapacity(4) - 1u;
    ASSERT_EQ(mask, 7u);
    const std::uint32_t home = Glitch::subjectTableHome(1u, mask);
    for (std::uint64_t id = 1u; ids.size() < 5; ++id) {
        if (Glitch::subjectTableHome(id, mask) == home) {
            ids.push_back(id);
        }
    }
    const std::vector<std::uint64_t> stored(ids.begin(), ids.begin() + 4);
    std::vector<uvec4> words;
    Glitch::packSubjectWords(records(stored, tearSubject()), 0u, Glitch::toTickWords(0), words);
    for (std::size_t slot = 0; slot < stored.size(); ++slot) {
        EXPECT_EQ(Glitch::lookupSubjectSlot(words, stored[slot]), static_cast<int>(slot));
    }
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, ids[4]), -1)
        << "a colliding absent id probes to an empty entry";
}

TEST(TrixelGlitchMathTest, CarrierBitsAreStrippedBeforeLookup) {
    const std::uint64_t id = 0x1'0000'002Aull;
    std::vector<uvec4> words;
    Glitch::packSubjectWords(records({id}, tearSubject()), 0u, Glitch::toTickWords(0), words);
    // Priority tier, fog BODY class + factor and the cardinal-voxel flag all
    // ride the high word of the stored id.
    const IRMath::uvec2 carried{
        Glitch::entityIdLow(id),
        Glitch::entityIdHigh(id) | IRRender::kEntityIdPriorityMaskInHighWord |
            IRRender::kEntityIdFogBodyMaskInHighWord |
            IRRender::kEntityIdCardinalVoxelMaskInHighWord
    };
    const std::uint64_t raw =
        static_cast<std::uint64_t>(carried.x) | (static_cast<std::uint64_t>(carried.y) << 32);
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, raw), -1) << "raw equality misses";
    EXPECT_EQ(Glitch::lookupSubjectSlot(words, IRRender::decodeCarrierEntityId(carried)), 0);
}

TEST(TrixelGlitchMathTest, HashesMatchFixedVectors) {
    EXPECT_EQ(Glitch::subjectSalt(1000u), 0x87D7CA39u);
    EXPECT_EQ(Glitch::subjectTableHome(1000u, 7u), 7u);
    EXPECT_EQ(Glitch::pairHash(7u, Glitch::toTickWords(3), 1000u, 2000u), 0x8EE6EAF7u);
    EXPECT_EQ(Glitch::pairMode(0x8EE6EAF7u), Glitch::PairMode::MIX);
    EXPECT_EQ(
        Glitch::pairHash(0xFFFFFFFFu, Glitch::toTickWords(-1), 0x1'0000'0005ull, 0x1'0000'0009ull),
        0x4C68E06Bu
    );
    EXPECT_NE(
        Glitch::pairHash(7u, Glitch::toTickWords(3), 1000u, 2000u),
        Glitch::pairHash(7u, Glitch::toTickWords(4), 1000u, 2000u)
    ) << "the pair re-rolls every period tick";
}

TEST(TrixelGlitchMathTest, EveryOffsetIsAWholeTrixelWithinItsBound) {
    for (std::uint64_t id = 1u; id < 64u; ++id) {
        for (int spill = 0; spill <= Glitch::kSubjectMaxSpill; ++spill) {
            C_GlitchSubject subject = tearSubject(spill);
            subject.splitWeight_ = 1.0f;
            subject.echoWeight_ = 1.0f;
            const Glitch::SubjectRecord record = Glitch::subjectRecord(
                id,
                subject,
                9u,
                Glitch::toTickWords(static_cast<std::int64_t>(id))
            );
            EXPECT_GE(record.tearMaxTrixels_, 1);
            EXPECT_LE(record.tearMaxTrixels_, Glitch::kSubjectMaxTearTrixels);
            EXPECT_NE(record.splitTrixels_, 0);
            EXPECT_LE(IRMath::abs(record.splitTrixels_), Glitch::kSubjectMaxSplitTrixels);
            const int reach = IRMath::max(1, spill);
            for (const ivec2 echo : {record.echoA_, record.echoB_}) {
                const int norm = IRMath::max(IRMath::abs(echo.x), IRMath::abs(echo.y));
                EXPECT_GE(norm, 1);
                EXPECT_LE(norm, reach);
                // The kernel's edge test reads one texel past the echo offset.
                EXPECT_LE(norm + 1, Glitch::kSubjectHalo);
            }
            EXPECT_NE(record.echoA_, record.echoB_);
            EXPECT_GE(record.bandRows_, 1);
            EXPECT_LE(record.bandRows_, Glitch::kSubjectBandRowsSpan);
        }
    }
}

// ---- closest-two influence ----------------------------------------------------

// The reference rule, computed independently: each subject's minimum
// Chebyshev distance within its spill, sorted by (distance, slot).
Glitch::Influence referenceInfluence(
    ivec2 p, const std::map<std::pair<int, int>, int> &slots, const std::vector<int> &spill
) {
    std::map<int, int> best;
    for (const auto &[at, slot] : slots) {
        const int dist = IRMath::max(IRMath::abs(at.first - p.x), IRMath::abs(at.second - p.y));
        if (dist > spill[static_cast<std::size_t>(slot)]) {
            continue;
        }
        auto found = best.find(slot);
        if (found == best.end() || dist < found->second) {
            best[slot] = dist;
        }
    }
    std::vector<std::pair<int, int>> ranked;
    for (const auto &[slot, dist] : best) {
        ranked.push_back({dist, slot});
    }
    std::sort(ranked.begin(), ranked.end());
    Glitch::Influence out;
    out.count_ = static_cast<int>(IRMath::min<std::size_t>(ranked.size(), 2));
    if (!ranked.empty()) {
        out.slotA_ = ranked[0].second;
        out.distA_ = ranked[0].first;
    }
    if (ranked.size() > 1) {
        out.slotB_ = ranked[1].second;
        out.distB_ = ranked[1].first;
    }
    return out;
}

TEST(TrixelGlitchMathTest, ClosestTwoMatchesTheReferenceOnScatteredSubjects) {
    std::uint32_t state = 12345u;
    const auto next = [&state]() {
        state = Glitch::hash3(state, 0x51u, 0x7Fu);
        return state;
    };
    for (int trial = 0; trial < 200; ++trial) {
        const int subjectCount = 2 + static_cast<int>(next() % 4u);
        std::vector<int> spill;
        for (int s = 0; s < subjectCount; ++s) {
            spill.push_back(static_cast<int>(next() % 5u));
        }
        std::map<std::pair<int, int>, int> slots;
        for (int texel = 0; texel < 40; ++texel) {
            slots[{static_cast<int>(next() % 13u), static_cast<int>(next() % 13u)}] =
                static_cast<int>(next() % static_cast<std::uint32_t>(subjectCount));
        }
        const ivec2 p(6, 6);
        const Glitch::Influence got = Glitch::resolveInfluence(
            p,
            Glitch::kSubjectMaxSpill,
            [&slots](ivec2 q) {
                const auto found = slots.find({q.x, q.y});
                return found == slots.end() ? -1 : found->second;
            },
            [&spill](int slot) { return spill[static_cast<std::size_t>(slot)]; }
        );
        const Glitch::Influence want = referenceInfluence(p, slots, spill);
        ASSERT_EQ(got.count_, want.count_) << "trial " << trial;
        EXPECT_EQ(got.slotA_, want.slotA_) << "trial " << trial;
        EXPECT_EQ(got.distA_, want.distA_) << "trial " << trial;
        EXPECT_EQ(got.slotB_, want.slotB_) << "trial " << trial;
        EXPECT_EQ(got.distB_, want.distB_) << "trial " << trial;
    }
}

TEST(TrixelGlitchMathTest, EqualDistancesBreakTiesByAscendingEntityId) {
    // Slots 2, 0 and 1 all at Chebyshev distance 2, each within spill 2.
    const std::map<std::pair<int, int>, int> slots{{{2, 0}, 2}, {{0, 2}, 0}, {{4, 4}, 1}};
    const Glitch::Influence got = Glitch::resolveInfluence(
        ivec2(2, 2),
        Glitch::kSubjectMaxSpill,
        [&slots](ivec2 q) {
            const auto found = slots.find({q.x, q.y});
            return found == slots.end() ? -1 : found->second;
        },
        [](int) { return 2; }
    );
    EXPECT_EQ(got.count_, 2);
    EXPECT_EQ(got.slotA_, 0);
    EXPECT_EQ(got.slotB_, 1);
    EXPECT_EQ(got.distA_, 2);
    EXPECT_EQ(got.distB_, 2);
}

TEST(TrixelGlitchMathTest, StepTwoSpillVisitsOddOddFaceNeighbours) {
    const ivec2 p(8, 4);
    const ivec2 diagonal = p + ivec2(1, 1);
    const Glitch::Influence got = Glitch::resolveInfluenceStepped(
        p,
        1,
        Glitch::kPerAxisFaceStep,
        [diagonal](ivec2 q) { return q == diagonal ? 0 : -1; },
        [](int) { return 1; }
    );
    ASSERT_EQ(got.count_, 1);
    EXPECT_EQ(got.slotA_, 0);
    EXPECT_EQ(got.distA_, 1);
}

TEST(TrixelGlitchMathTest, PerAxisTileMarkCoversReachAcrossTileBoundaries) {
    constexpr IRMath::ivec2 kStoreSize{53, 41};
    constexpr IRMath::ivec2 kSubjectCell{15, 16};
    constexpr int kSpill = IRPrefab::Glitch::kSubjectMaxSpill;
    const IRMath::ivec4 bounds =
        IRPrefab::Glitch::perAxisSubjectTileBounds(kSubjectCell, kSpill, kStoreSize);
    const int reach = IRPrefab::Glitch::kPerAxisFaceStep * kSpill;
    for (int y = IRMath::max(0, kSubjectCell.y - reach);
         y <= IRMath::min(kStoreSize.y - 1, kSubjectCell.y + reach);
         ++y) {
        for (int x = IRMath::max(0, kSubjectCell.x - reach);
             x <= IRMath::min(kStoreSize.x - 1, kSubjectCell.x + reach);
             ++x) {
            const int tileX = x / IRPrefab::Glitch::kPerAxisSubjectTileSize;
            const int tileY = y / IRPrefab::Glitch::kPerAxisSubjectTileSize;
            EXPECT_GE(tileX, bounds.x);
            EXPECT_LE(tileX, bounds.z);
            EXPECT_GE(tileY, bounds.y);
            EXPECT_LE(tileY, bounds.w);
        }
    }
    EXPECT_LT(bounds.x, bounds.z) << "the subject reach must cross the x tile boundary";
    EXPECT_LT(bounds.y, bounds.w) << "the subject reach must cross the y tile boundary";
}

// ---- the per-texel CPU twin ------------------------------------------------

struct FakeCanvas {
    ivec2 size_{24, 8};
    std::vector<uvec4> colours_ = std::vector<uvec4>(24 * 8, uvec4(0u));
    std::vector<std::uint64_t> ids_ = std::vector<std::uint64_t>(24 * 8, 0u);
    std::vector<int> distances_ = std::vector<int>(24 * 8, Glitch::kSubjectEmptyDistance);
    std::vector<uvec4> historyColours_ = std::vector<uvec4>(24 * 8, uvec4(0u));
    std::vector<std::uint64_t> historyIds_ = std::vector<std::uint64_t>(24 * 8, 0u);

    std::size_t index(ivec2 q) const {
        return static_cast<std::size_t>(q.y * size_.x + q.x);
    }
    ivec2 size() const {
        return size_;
    }
    uvec4 colour(ivec2 q) const {
        return colours_[index(q)];
    }
    std::uint64_t id(ivec2 q) const {
        return ids_[index(q)];
    }
    int distance(ivec2 q) const {
        return distances_[index(q)];
    }
    uvec4 historyColour(ivec2 q) const {
        return historyColours_[index(q)];
    }
    std::uint64_t historyId(ivec2 q) const {
        return historyIds_[index(q)];
    }

    // Columns [x0, x1) of every row belong to @p id, each texel coloured by
    // its column so a displaced read is visible.
    void paint(int x0, int x1, std::uint64_t id, int depth = 0) {
        for (int y = 0; y < size_.y; ++y) {
            for (int x = x0; x < x1; ++x) {
                colours_[index(ivec2(x, y))] =
                    uvec4(static_cast<std::uint32_t>(10 * x), 50u, 200u, 255u);
                ids_[index(ivec2(x, y))] = id;
                distances_[index(ivec2(x, y))] = depth * 8;
            }
        }
    }
};

struct TwinRun {
    std::vector<Glitch::SubjectRecord> records_;
    std::vector<uvec4> words_;

    TwinRun(const std::vector<std::pair<std::uint64_t, C_GlitchSubject>> &subjects) {
        for (const auto &[id, subject] : subjects) {
            records_.push_back(Glitch::subjectRecord(id, subject, 21u, Glitch::toTickWords(2)));
        }
        Glitch::packSubjectWords(records_, 21u, Glitch::toTickWords(2), words_);
    }

    Glitch::SubjectFrame<FakeCanvas> frame(const FakeCanvas &canvas) const {
        return {
            canvas,
            records_,
            21u,
            Glitch::toTickWords(2),
            static_cast<int>(words_[1].w),
            words_
        };
    }
};

TEST(TrixelGlitchMathTest, StepOneRouteIsTheMainCanvasTwinBitForBit) {
    FakeCanvas canvas;
    canvas.paint(6, 18, 100u);
    C_GlitchSubject subject = tearSubject(3);
    subject.holdWeight_ = 1.0f;
    subject.splitWeight_ = 1.0f;
    subject.echoWeight_ = 1.0f;
    subject.dissolveWeight_ = 0.5f;
    const TwinRun run({{100u, subject}});
    const auto shipped = run.frame(canvas);
    auto routed = run.frame(canvas);
    routed.spatialStep_ = 1;
    routed.storeOrigin_ = ivec2(0);
    routed.historyOrigin_ = ivec2(0);
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            Glitch::TexelTrace shippedTrace;
            Glitch::TexelTrace routedTrace;
            EXPECT_EQ(
                shipped.texel(ivec2(x, y), shippedTrace),
                routed.texel(ivec2(x, y), routedTrace)
            ) << x
              << "," << y;
        }
    }
}

TEST(TrixelGlitchMathTest, StepTwoOffsetsStayOnTheOccupiedSublattice) {
    FakeCanvas canvas;
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 2; x < canvas.size_.x - 2; ++x) {
            if (((x + y) & 1) == 0) {
                const std::size_t index = canvas.index(ivec2(x, y));
                canvas.ids_[index] = 100u;
                canvas.colours_[index] = uvec4(static_cast<std::uint32_t>(x), 50u, 200u, 255u);
            }
        }
    }
    C_GlitchSubject subject = tearSubject(4);
    subject.splitWeight_ = 1.0f;
    const TwinRun run({{100u, subject}});
    auto frame = run.frame(canvas);
    frame.spatialStep_ = Glitch::kPerAxisFaceStep;
    int displaced = 0;
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 2; x < canvas.size_.x - 2; ++x) {
            if (((x + y) & 1) != 0) {
                continue;
            }
            Glitch::TexelTrace trace;
            frame.texel(ivec2(x, y), trace);
            for (const int dx : {trace.tearDx_, trace.splitDx_}) {
                EXPECT_EQ(IRMath::floorMod(dx, Glitch::kPerAxisFaceStep), 0);
                displaced += dx != 0 ? 1 : 0;
            }
        }
    }
    EXPECT_GT(displaced, 0);
}

TEST(TrixelGlitchMathTest, PerAxisOutputIsTranslationEquivariantWithStoreOrigin) {
    FakeCanvas base;
    FakeCanvas shifted;
    constexpr ivec2 delta(2, 0);
    for (int y = 1; y < base.size_.y - 1; ++y) {
        for (int x = 4; x < 16; ++x) {
            if (((x + y) & 1) != 0) {
                continue;
            }
            const ivec2 p(x, y);
            const ivec2 q = p + delta;
            base.ids_[base.index(p)] = 100u;
            shifted.ids_[shifted.index(q)] = 100u;
            const uvec4 colour(static_cast<std::uint32_t>(x * 7), 30u, 90u, 255u);
            base.colours_[base.index(p)] = colour;
            shifted.colours_[shifted.index(q)] = colour;
        }
    }
    C_GlitchSubject subject = tearSubject(2);
    subject.splitWeight_ = 1.0f;
    subject.dissolveWeight_ = 0.5f;
    const TwinRun run({{100u, subject}});
    auto a = run.frame(base);
    auto b = run.frame(shifted);
    a.spatialStep_ = b.spatialStep_ = Glitch::kPerAxisFaceStep;
    b.storeOrigin_ = delta;
    for (int y = 1; y < base.size_.y - 1; ++y) {
        for (int x = 4; x < 16; ++x) {
            const ivec2 p(x, y);
            Glitch::TexelTrace traceA;
            Glitch::TexelTrace traceB;
            EXPECT_EQ(a.texel(p, traceA), b.texel(p + delta, traceB)) << x << "," << y;
        }
    }
}

TEST(TrixelGlitchMathTest, PerAxisHoldReadsThroughTheSnapshotOriginDelta) {
    FakeCanvas canvas;
    const ivec2 current(12, 4);
    const ivec2 old(10, 4);
    canvas.ids_[canvas.index(current)] = 100u;
    canvas.colours_[canvas.index(current)] = uvec4(8u, 8u, 8u, 255u);
    canvas.historyIds_[canvas.index(old)] = 100u;
    canvas.historyColours_[canvas.index(old)] = uvec4(1u, 2u, 3u, 255u);
    C_GlitchSubject subject;
    subject.holdWeight_ = 1.0f;
    const TwinRun run({{100u, subject}});
    auto frame = run.frame(canvas);
    frame.spatialStep_ = Glitch::kPerAxisFaceStep;
    frame.storeOrigin_ = ivec2(2, 0);
    frame.historyOrigin_ = ivec2(0);
    Glitch::TexelTrace trace;
    EXPECT_EQ(frame.texel(current, trace), uvec4(1u, 2u, 3u, 255u));
    EXPECT_TRUE(trace.held_);
    EXPECT_FALSE(trace.heldForeign_);
}

TEST(TrixelGlitchMathTest, TearAndSplitReadOnlyTheSubjectsOwnTrixels) {
    FakeCanvas canvas;
    canvas.paint(8, 14, 100u);
    canvas.paint(14, 18, 200u);
    C_GlitchSubject subject = tearSubject(Glitch::kSubjectMaxSpill);
    subject.splitWeight_ = 1.0f;
    const TwinRun run({{100u, subject}});
    const auto frame = run.frame(canvas);
    int tears = 0;
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            Glitch::TexelTrace trace;
            const uvec4 out = frame.texel(ivec2(x, y), trace);
            if (trace.tearDx_ != 0) {
                ++tears;
                EXPECT_EQ(canvas.id(ivec2(x + trace.tearDx_, y)), 100u)
                    << "a tear accepts only the subject's trixels";
            }
            if (canvas.id(ivec2(x, y)) == 200u && trace.influences_ > 0) {
                // The neighbour inside the spill band only ever shows its own
                // colour or the subject's.
                const bool own = out == canvas.colour(ivec2(x, y));
                bool subjects = false;
                for (int sx = 8; sx < 14; ++sx) {
                    subjects = subjects || out.r == canvas.colour(ivec2(sx, y)).r;
                }
                EXPECT_TRUE(own || subjects) << x << "," << y;
            }
            if (trace.influences_ == 0) {
                EXPECT_EQ(out, canvas.colour(ivec2(x, y))) << "outside every band nothing moves";
            }
        }
    }
    EXPECT_GT(tears, 0);
}

TEST(TrixelGlitchMathTest, HoldNeverShowsAnotherEntitysHistory) {
    FakeCanvas canvas;
    canvas.paint(8, 12, 100u);
    // History: the subject one column left, and a foreign entity where the
    // subject now stands at column 11.
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 7; x < 11; ++x) {
            canvas.historyIds_[canvas.index(ivec2(x, y))] = 100u;
            canvas.historyColours_[canvas.index(ivec2(x, y))] = uvec4(1u, 2u, 3u, 255u);
        }
        canvas.historyIds_[canvas.index(ivec2(11, y))] = 300u;
        canvas.historyColours_[canvas.index(ivec2(11, y))] = uvec4(9u, 9u, 9u, 255u);
    }
    C_GlitchSubject subject;
    subject.holdWeight_ = 1.0f;
    subject.spill_ = 1;
    const TwinRun run({{100u, subject}});
    const auto frame = run.frame(canvas);
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 6; x < 13; ++x) {
            Glitch::TexelTrace trace;
            const uvec4 out = frame.texel(ivec2(x, y), trace);
            EXPECT_FALSE(trace.heldForeign_);
            EXPECT_NE(out, uvec4(9u, 9u, 9u, 255u)) << "foreign history at " << x;
            if (x >= 7 && x < 11) {
                EXPECT_EQ(out, uvec4(1u, 2u, 3u, 255u)) << "the subject's history at " << x;
            }
        }
    }
}

TEST(TrixelGlitchMathTest, DepthBandIsInclusiveAndSkipsEmptyTexels) {
    FakeCanvas canvas;
    canvas.paint(0, 24, 100u);
    // Rows 0..7 carry decoded depths 9..16; the band is [11, 14].
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            canvas.distances_[canvas.index(ivec2(x, y))] = (9 + y) * 8 + 5;
        }
    }
    C_GlitchSubject subject;
    subject.holdWeight_ = 1.0f;
    subject.depthEnabled_ = true;
    subject.depthNear_ = 11;
    subject.depthFar_ = 14;
    for (auto &historyId : canvas.historyIds_) {
        historyId = 100u;
    }
    const TwinRun run({{100u, subject}});
    const auto frame = run.frame(canvas);
    for (int y = 0; y < canvas.size_.y; ++y) {
        Glitch::TexelTrace trace;
        frame.texel(ivec2(3, y), trace);
        const int depth = 9 + y;
        EXPECT_EQ(trace.held_, depth >= 11 && depth <= 14) << "depth " << depth;
    }
    canvas.distances_[canvas.index(ivec2(3, 3))] = Glitch::kSubjectEmptyDistance;
    Glitch::TexelTrace empty;
    run.frame(canvas).texel(ivec2(3, 3), empty);
    EXPECT_FALSE(empty.held_) << "the empty distance never participates";
}

TEST(TrixelGlitchMathTest, DissolveDropsOnlyTheSubjectsCells) {
    FakeCanvas canvas;
    canvas.paint(4, 10, 100u);
    canvas.paint(10, 16, 200u);
    C_GlitchSubject subject;
    subject.dissolveWeight_ = 1.0f;
    subject.spill_ = Glitch::kSubjectMaxSpill;
    const TwinRun run({{100u, subject}});
    const auto frame = run.frame(canvas);
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            Glitch::TexelTrace trace;
            const uvec4 out = frame.texel(ivec2(x, y), trace);
            if (canvas.id(ivec2(x, y)) == 100u) {
                EXPECT_TRUE(trace.dissolved_) << "weight 1 drops every subject cell";
                EXPECT_EQ(out, uvec4(0u));
            } else {
                EXPECT_FALSE(trace.dissolved_);
                EXPECT_EQ(out, canvas.colour(ivec2(x, y)));
            }
        }
    }
}

TEST(TrixelGlitchMathTest, EchoTintsOnlyOffsetEdgesInsideTheBand) {
    FakeCanvas canvas;
    canvas.size_ = ivec2(24, 24);
    canvas.colours_.assign(24 * 24, uvec4(0u));
    canvas.ids_.assign(24 * 24, 0u);
    canvas.distances_.assign(24 * 24, Glitch::kSubjectEmptyDistance);
    canvas.historyColours_.assign(24 * 24, uvec4(0u));
    canvas.historyIds_.assign(24 * 24, 0u);
    for (int y = 9; y < 15; ++y) {
        for (int x = 9; x < 15; ++x) {
            canvas.ids_[canvas.index(ivec2(x, y))] = 100u;
            canvas.colours_[canvas.index(ivec2(x, y))] = uvec4(40u, 40u, 40u, 255u);
        }
    }
    C_GlitchSubject subject;
    subject.echoWeight_ = 1.0f;
    subject.spill_ = 3;
    const TwinRun run({{100u, subject}});
    const auto frame = run.frame(canvas);
    int echoed = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            Glitch::TexelTrace trace;
            frame.texel(ivec2(x, y), trace);
            if (trace.echoed_) {
                ++echoed;
                EXPECT_GT(trace.influences_, 0) << "echo stays inside the spill band";
            }
        }
    }
    EXPECT_GT(echoed, 0);
}

TEST(TrixelGlitchMathTest, OverlappingBandsInterfereAndSeparatedBandsDoNot) {
    C_GlitchSubject subject = tearSubject(Glitch::kSubjectMaxSpill);
    subject.echoWeight_ = 1.0f;
    const TwinRun run({{100u, subject}, {200u, subject}});
    for (const int gap : {9, 6}) {
        FakeCanvas canvas;
        canvas.size_ = ivec2(40, 8);
        canvas.colours_.assign(40 * 8, uvec4(0u));
        canvas.ids_.assign(40 * 8, 0u);
        canvas.distances_.assign(40 * 8, Glitch::kSubjectEmptyDistance);
        canvas.historyColours_.assign(40 * 8, uvec4(0u));
        canvas.historyIds_.assign(40 * 8, 0u);
        canvas.paint(6, 12, 100u);
        canvas.paint(12 + gap, 18 + gap, 200u);
        const auto frame = run.frame(canvas);
        int interfered = 0;
        for (int y = 0; y < canvas.size_.y; ++y) {
            for (int x = 0; x < canvas.size_.x; ++x) {
                Glitch::TexelTrace trace;
                frame.texel(ivec2(x, y), trace);
                interfered += trace.influences_ == 2 ? 1 : 0;
            }
        }
        if (gap > 2 * Glitch::kSubjectMaxSpill) {
            EXPECT_EQ(interfered, 0) << "gap " << gap;
        } else {
            EXPECT_GT(interfered, 0) << "gap " << gap;
        }
    }
}

// ---- shader twin constants ---------------------------------------------------

std::string readShader(const std::string &relative) {
    std::ifstream file(std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + relative);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

TEST(TrixelGlitchMathTest, ShaderTwinsCarryTheCpuConstants) {
    const std::string expected[] = {
        "kGlitchEffectSubjectTear = " + std::to_string(Glitch::kEffectSubjectTear) + "u;",
        "kGlitchEffectSubjectHold = " + std::to_string(Glitch::kEffectSubjectHold) + "u;",
        "kGlitchEffectSubjectDissolve = " + std::to_string(Glitch::kEffectSubjectDissolve) + "u;",
        "kGlitchEffectSubjectPair = " + std::to_string(Glitch::kEffectSubjectPair) + "u;",
        "kGlitchSubjectTableTag = 0x2C1B3C6Du;",
        "kGlitchPairModeCount = " + std::to_string(Glitch::kPairModeCount) + "u;",
        "kGlitchSubjectMaxSpill = " + std::to_string(Glitch::kSubjectMaxSpill) + ";",
        "kGlitchSubjectHalo = " + std::to_string(Glitch::kSubjectHalo) + ";",
        "kGlitchSubjectTileSize = " + std::to_string(Glitch::kSubjectTileSize) + ";",
        "kGlitchSubjectRecordWords = " + std::to_string(Glitch::kSubjectRecordWords) + "u;",
        "kGlitchSubjectEmptyDistance = " + std::to_string(Glitch::kSubjectEmptyDistance) + ";",
        "kGlitchSubjectTintPaletteSize = " + std::to_string(Glitch::kSubjectTintPaletteSize) + ";",
    };
    static_assert(Glitch::kSubjectTableTag == 0x2C1B3C6Du);
    for (const char *twin : {"ir_glitch_common.glsl", "metal/ir_glitch_common.metal"}) {
        const std::string source = readShader(twin);
        ASSERT_FALSE(source.empty()) << twin << " not found";
        for (const std::string &line : expected) {
            EXPECT_NE(source.find(line), std::string::npos) << twin << " lacks `" << line << "`";
        }
        for (int tint = 0; tint < Glitch::kSubjectTintPaletteSize; ++tint) {
            const uvec4 rgba = Glitch::subjectTint(tint);
            const std::string literal = std::to_string(rgba.r) + "u, " + std::to_string(rgba.g) +
                                        "u, " + std::to_string(rgba.b) + "u, 255u";
            EXPECT_NE(source.find(literal), std::string::npos) << twin << " tint " << tint;
        }
    }
    for (const char *kernel :
         {"c_trixel_glitch.glsl",
          "metal/c_trixel_glitch.metal",
          "c_trixel_glitch_per_axis.glsl",
          "metal/c_trixel_glitch_per_axis.metal"}) {
        const std::string source = readShader(kernel);
        ASSERT_FALSE(source.empty()) << kernel << " not found";
        EXPECT_NE(
            source.find(
                kernel[0] == 'c'
                    ? "binding = " + std::to_string(IRRender::kBufferIndex_TrixelGlitchSubjects) +
                          ")"
                    : "[[buffer(" + std::to_string(IRRender::kBufferIndex_TrixelGlitchSubjects) +
                          ")]]"
            ),
            std::string::npos
        ) << kernel
          << " binds the subject buffer elsewhere";
    }
    EXPECT_NE(
        readShader("c_trixel_glitch.glsl")
            .find(
                "local_size_x = " + std::to_string(Glitch::kTrixelGlitchGroupSize) +
                ", local_size_y = " + std::to_string(Glitch::kTrixelGlitchGroupSize)
            ),
        std::string::npos
    );
    EXPECT_NE(
        readShader("c_trixel_glitch_per_axis.glsl")
            .find(
                "local_size_x = " + std::to_string(Glitch::kTrixelGlitchGroupSize) +
                ", local_size_y = " + std::to_string(Glitch::kTrixelGlitchGroupSize)
            ),
        std::string::npos
    );
    for (const char *kernel : {
             "c_trixel_glitch_per_axis_mark.glsl",
             "c_trixel_glitch_per_axis.glsl",
             "c_trixel_glitch_per_axis_writeback.glsl",
             "metal/c_trixel_glitch_per_axis_mark.metal",
             "metal/c_trixel_glitch_per_axis.metal",
             "metal/c_trixel_glitch_per_axis_writeback.metal",
         }) {
        const std::string source = readShader(kernel);
        ASSERT_FALSE(source.empty()) << kernel << " not found";
        EXPECT_NE(source.find("16"), std::string::npos) << kernel << " lacks the tile size";
    }
    for (const char *kernel : {
             "c_trixel_glitch_per_axis.glsl",
             "metal/c_trixel_glitch_per_axis.metal",
         }) {
        const std::string source = readShader(kernel);
        EXPECT_NE(source.find("TileHalo = 10"), std::string::npos)
            << kernel << " lacks the staged halo";
        EXPECT_NE(source.find("TileSize + 2 *"), std::string::npos)
            << kernel << " does not derive the staged window from tile plus halo";
    }
}

} // namespace
