#include <gtest/gtest.h>

#include <irreden/spatial/field_placement.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace IRPrefab::Spatial {

void PrintTo(const PlacementHit &hit, std::ostream *os) {
    *os << "{{" << hit.cell_.x << ", " << hit.cell_.y << "}, {" << hit.chunk_.x << ", "
        << hit.chunk_.y << "}}";
}

void PrintTo(const PlacementQueryStats &stats, std::ostream *os) {
    *os << "{" << stats.chunksConsidered_ << ", " << stats.chunksPruned_ << ", "
        << stats.candidatesDrawn_ << "}";
}

} // namespace IRPrefab::Spatial

namespace {

using IRMath::ivec2;
using IRPrefab::Spatial::ChunkedField2D;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::fieldChunkOf;
using IRPrefab::Spatial::kFieldChunkEdge;
using IRPrefab::Spatial::kMaxClearanceCells;
using IRPrefab::Spatial::kMaxPlacementHits;
using IRPrefab::Spatial::kPlacementAttempts;
using IRPrefab::Spatial::packFieldChunkKey;
using IRPrefab::Spatial::PlacementField;
using IRPrefab::Spatial::PlacementHit;
using IRPrefab::Spatial::PlacementParams;
using IRPrefab::Spatial::PlacementQueryStats;
using IRPrefab::Spatial::queryPlacements;

// ---- fixtures: every field below is built by deterministic construction ----

void addFreeChunks(PlacementField &field, ivec2 chunkMin, ivec2 chunkMax) {
    for (int y = chunkMin.y; y <= chunkMax.y; ++y) {
        for (int x = chunkMin.x; x <= chunkMax.x; ++x) {
            field.setCell(ivec2{x, y} * kFieldChunkEdge, 0);
        }
    }
}

void fillChunk(PlacementField &field, ivec2 chunk, std::uint8_t value) {
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        for (int x = 0; x < kFieldChunkEdge; ++x) {
            field.setCell(chunk * kFieldChunkEdge + ivec2{x, y}, value);
        }
    }
}

// 3x3 free field chunks around the origin, cells [-32, 64)^2.
PlacementField makeFreeField(int maxClearance = 2) {
    PlacementField field(maxClearance);
    addFreeChunks(field, {-1, -1}, {1, 1});
    field.update();
    return field;
}

PlacementParams freeParams() {
    PlacementParams params;
    params.anchor_ = {16, 16};
    params.k_ = 4;
    params.minSpacing_ = 4;
    params.clearance_ = 1;
    params.seed_ = 1;
    return params;
}

// The same 3x3 free block with a wall at x = 40 splitting it into two regions
// and six 2x2 pillars scattered through the anchor's side.
PlacementField makeEndToEndField() {
    PlacementField field(4);
    addFreeChunks(field, {-1, -1}, {1, 1});
    for (int y = -32; y < 64; ++y) {
        field.setCell({40, y}, 1);
    }
    for (ivec2 pillar :
         {ivec2{0, 0},
          ivec2{12, -10},
          ivec2{-20, 20},
          ivec2{20, 30},
          ivec2{5, 50},
          ivec2{-10, -25}}) {
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                field.setCell(pillar + ivec2{x, y}, 1);
            }
        }
    }
    field.update();
    return field;
}

PlacementParams endToEndParams() {
    PlacementParams params;
    params.anchor_ = {30, 2};
    params.k_ = 8;
    params.minSpacing_ = 4;
    params.clearance_ = 2;
    params.sameRegionAsAnchor_ = true;
    params.seed_ = 12345;
    return params;
}

// One row of three free field chunks, cells x in [0, 96), y in [0, 32), with a
// full-height wall at x = 12: the anchor's region is the 12-wide strip to its
// left, the far region everything to the right.
constexpr int kWallX = 12;

PlacementField makeTwoRegionField() {
    PlacementField field(2);
    addFreeChunks(field, {0, 0}, {2, 0});
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        field.setCell({kWallX, y}, 1);
    }
    field.update();
    return field;
}

PlacementParams twoRegionParams() {
    PlacementParams params;
    params.anchor_ = {6, 16};
    params.k_ = 100;
    params.minSpacing_ = 3;
    params.clearance_ = 1;
    params.sameRegionAsAnchor_ = true;
    params.seed_ = 2024;
    return params;
}

int countFarRegionHits(const std::vector<PlacementHit> &hits) {
    return static_cast<int>(std::count_if(hits.begin(), hits.end(), [](const PlacementHit &hit) {
        return hit.cell_.x > kWallX;
    }));
}

// A single fully occupied field chunk with the listed cells freed.
PlacementField makeOccupiedChunkWithFreeCells(std::initializer_list<ivec2> freeCells) {
    PlacementField field(1);
    fillChunk(field, {0, 0}, 1);
    for (ivec2 cell : freeCells) {
        field.setCell(cell, 0);
    }
    field.update();
    return field;
}

PlacementParams occupiedChunkParams() {
    PlacementParams params;
    params.anchor_ = {16, 16};
    params.k_ = 8;
    params.minSpacing_ = 1;
    params.clearance_ = 0;
    params.seed_ = 1;
    return params;
}

// 5x5 fully occupied field chunks, except a 16x16 free block in the low corner
// of the centre chunk (cells [64, 80)^2).
constexpr int kPruningChunkCount = 25;

PlacementField makePruningField() {
    PlacementField field(2);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            fillChunk(field, {x, y}, 1);
        }
    }
    for (int y = 64; y < 80; ++y) {
        for (int x = 64; x < 80; ++x) {
            field.setCell({x, y}, 0);
        }
    }
    field.update();
    return field;
}

PlacementParams pruningParams() {
    PlacementParams params;
    params.anchor_ = {66, 66};
    params.k_ = 8;
    params.minSpacing_ = 6;
    params.clearance_ = 2;
    params.seed_ = 12345;
    return params;
}

std::int64_t squaredDistance(ivec2 a, ivec2 b) {
    const std::int64_t dx = static_cast<std::int64_t>(a.x) - b.x;
    const std::int64_t dy = static_cast<std::int64_t>(a.y) - b.y;
    return dx * dx + dy * dy;
}

void expectPairwiseSpacing(const std::vector<PlacementHit> &hits, int minSpacing) {
    const std::int64_t spacingSq = static_cast<std::int64_t>(minSpacing) * minSpacing;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        for (std::size_t j = i + 1; j < hits.size(); ++j) {
            EXPECT_GE(squaredDistance(hits[i].cell_, hits[j].cell_), spacingSq)
                << "hits " << i << " and " << j;
        }
    }
}

// ---- in-test reference draw: an independent transcription of the locked ----
// ---- skeleton over the same field predicates, with two policy switches  ----

enum class RangeMap { MultiplyShift, Modulo };
enum class Selection { Uniform, Lifo };

struct ReferenceDraw {
    const PlacementField &field_;
    PlacementParams params_;
    RangeMap rangeMap_ = RangeMap::MultiplyShift;
    Selection selection_ = Selection::Uniform;

    IRMath::Pcg32 rng_{params_.seed_};
    std::vector<ivec2> samples_;
    std::vector<ivec2> active_;
    std::set<FieldChunkKey> chunksSeen_;
    std::vector<PlacementHit> hits_;
    PlacementQueryStats stats_;

    std::uint32_t below(std::uint32_t n) {
        const std::uint32_t word = rng_.nextWord();
        if (rangeMap_ == RangeMap::Modulo) {
            return word % n;
        }
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(word) * n) >> 32u);
    }

    ivec2 annulusOffset() {
        const int r = params_.minSpacing_;
        for (;;) {
            const int dx = static_cast<int>(below(static_cast<std::uint32_t>(4 * r + 1))) - 2 * r;
            const int dy = static_cast<int>(below(static_cast<std::uint32_t>(4 * r + 1))) - 2 * r;
            const std::int64_t d2 = std::int64_t{dx} * dx + std::int64_t{dy} * dy;
            const std::int64_t rr = std::int64_t{r} * r;
            if (d2 >= rr && d2 <= 4 * rr) {
                return {dx, dy};
            }
        }
    }

    bool spacingOk(ivec2 candidate) const {
        const std::int64_t spacingSq =
            static_cast<std::int64_t>(params_.minSpacing_) * params_.minSpacing_;
        return std::all_of(samples_.begin(), samples_.end(), [&](ivec2 sample) {
            return squaredDistance(sample, candidate) >= spacingSq;
        });
    }

    bool chunkMayHold(ivec2 chunk) {
        const std::int64_t clearanceSq =
            static_cast<std::int64_t>(params_.clearance_) * params_.clearance_;
        const auto *summary = field_.clearance().values().findChunk(chunk);
        const bool mayHold = summary != nullptr && summary->max_ >= clearanceSq;
        if (chunksSeen_.insert(packFieldChunkKey(chunk)).second) {
            ++stats_.chunksConsidered_;
            if (summary != nullptr && !mayHold) {
                ++stats_.chunksPruned_;
            }
        }
        return mayHold;
    }

    bool isValid(ivec2 cell) {
        if (!chunkMayHold(fieldChunkOf(cell))) {
            return false;
        }
        if (!field_.clearance().hasClearance(cell, params_.clearance_)) {
            return false;
        }
        return !params_.sameRegionAsAnchor_ || field_.regions().sameRegion(cell, params_.anchor_);
    }

    void run() {
        const ivec2 anchor = params_.anchor_;
        active_.push_back(anchor);
        samples_.push_back(anchor);
        if (isValid(anchor)) {
            hits_.push_back({anchor, fieldChunkOf(anchor)});
        }
        while (!active_.empty() && static_cast<int>(hits_.size()) < params_.k_) {
            const std::size_t i = selection_ == Selection::Lifo
                                      ? active_.size() - 1
                                      : below(static_cast<std::uint32_t>(active_.size()));
            const ivec2 s = active_[i];
            bool extended = false;
            for (int attempt = 0; attempt < kPlacementAttempts; ++attempt) {
                const ivec2 offset = annulusOffset();
                ++stats_.candidatesDrawn_;
                const std::int64_t cx = std::int64_t{s.x} + offset.x;
                const std::int64_t cy = std::int64_t{s.y} + offset.y;
                if (cx < std::numeric_limits<std::int32_t>::min() ||
                    cx > std::numeric_limits<std::int32_t>::max() ||
                    cy < std::numeric_limits<std::int32_t>::min() ||
                    cy > std::numeric_limits<std::int32_t>::max()) {
                    continue;
                }
                const ivec2 cand{static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy)};
                if (!spacingOk(cand) || !isValid(cand)) {
                    continue;
                }
                samples_.push_back(cand);
                active_.push_back(cand);
                hits_.push_back({cand, fieldChunkOf(cand)});
                extended = true;
                break;
            }
            if (!extended) {
                active_[i] = active_.back();
                active_.pop_back();
            }
        }
    }
};

// ---- grid width (D6) ----

TEST(PlacementGridWidthTest, MatchesFloorOfRadiusOverRootTwoAcrossTheDomain) {
    EXPECT_EQ(IRPrefab::Spatial::placementGridWidth(1), 1);
    for (int r = 2; r <= kMaxClearanceCells; ++r) {
        const int reference = static_cast<int>(std::floor(r / std::sqrt(2.0)));
        ASSERT_EQ(IRPrefab::Spatial::placementGridWidth(r), reference) << "r " << r;
    }
}

TEST(PlacementGridWidthTest, DivergesFromTruncatedConstantAtTheFiveKnownInputs) {
    const int inputs[] = {338, 577, 676, 915, 1014};
    const int expected[] = {239, 408, 478, 647, 717};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(IRPrefab::Spatial::placementGridWidth(inputs[i]), expected[i]);
        EXPECT_NE(
            IRPrefab::Spatial::placementGridWidth(inputs[i]),
            static_cast<int>(std::floor(inputs[i] * 0.7071))
        );
    }
}

// ---- domain rejection (D4), both arms ----

TEST(PlacementFieldTest, ConstructionEnforcesTheClearanceCapBothArms) {
    EXPECT_THROW(PlacementField(0), std::invalid_argument);
    EXPECT_THROW(PlacementField(kMaxClearanceCells + 1), std::invalid_argument);
    EXPECT_EQ(PlacementField(1).maxClearance(), 1);
    EXPECT_EQ(PlacementField(kMaxClearanceCells).maxClearance(), kMaxClearanceCells);
}

TEST(QueryPlacementsTest, RejectsEveryOutOfDomainParameterAndAcceptsTheAdjacentValues) {
    const PlacementField field = makeFreeField(2);
    std::vector<PlacementHit> out;

    EXPECT_THROW(queryPlacements(field, PlacementParams{}, out), std::invalid_argument);

    struct Arm {
        int PlacementParams::*member_;
        int value_;
        bool accepted_;
    };
    const Arm arms[] = {
        {&PlacementParams::minSpacing_, 0, false},
        {&PlacementParams::minSpacing_, kMaxClearanceCells + 1, false},
        {&PlacementParams::minSpacing_, 1, true},
        {&PlacementParams::minSpacing_, kMaxClearanceCells, true},
        {&PlacementParams::clearance_, -1, false},
        {&PlacementParams::clearance_, field.maxClearance() + 1, false},
        {&PlacementParams::clearance_, 0, true},
        {&PlacementParams::clearance_, field.maxClearance(), true},
        {&PlacementParams::k_, 0, false},
        {&PlacementParams::k_, -1, false},
        {&PlacementParams::k_, kMaxPlacementHits + 1, false},
        {&PlacementParams::k_, 1, true},
        {&PlacementParams::k_, kMaxPlacementHits, true},
    };
    for (const Arm &arm : arms) {
        PlacementParams params = freeParams();
        params.*arm.member_ = arm.value_;
        if (arm.accepted_) {
            EXPECT_NO_THROW(queryPlacements(field, params, out)) << arm.value_;
            EXPECT_GE(out.size(), 1u) << arm.value_;
        } else {
            EXPECT_THROW(queryPlacements(field, params, out), std::invalid_argument) << arm.value_;
        }
    }
}

// ---- pending-changes precondition ----

TEST(PlacementFieldTest, QueryRejectsPendingChangesUntilUpdate) {
    PlacementField field = makeFreeField();
    std::vector<PlacementHit> out;
    EXPECT_FALSE(field.hasPendingChanges());

    field.setCell({3, 3}, 1);
    EXPECT_TRUE(field.hasPendingChanges());
    EXPECT_THROW(queryPlacements(field, freeParams(), out), std::invalid_argument);

    field.update();
    EXPECT_FALSE(field.hasPendingChanges());
    queryPlacements(field, freeParams(), out);
    EXPECT_GE(out.size(), 1u);

    field.setCell({3, 3}, 1);
    EXPECT_FALSE(field.hasPendingChanges());
    queryPlacements(field, freeParams(), out);
    EXPECT_GE(out.size(), 1u);
}

TEST(ChunkedFieldTest, HasDirtyKeysAgreesWithTheDirtySnapshot) {
    ChunkedField2D<std::uint8_t> field;
    std::vector<FieldChunkKey> keys;
    const auto expectAgreement = [&] {
        field.dirtyKeys(keys);
        EXPECT_EQ(field.hasDirtyKeys(), !keys.empty());
    };

    expectAgreement();
    EXPECT_FALSE(field.hasDirtyKeys());
    field.setCell({1, 1}, 1);
    expectAgreement();
    EXPECT_TRUE(field.hasDirtyKeys());
    field.update();
    expectAgreement();
    EXPECT_FALSE(field.hasDirtyKeys());
    field.setCell({1, 1}, 1);
    expectAgreement();
    EXPECT_FALSE(field.hasDirtyKeys());
    field.clear();
    expectAgreement();
    EXPECT_TRUE(field.hasDirtyKeys());
    field.update();
    expectAgreement();
    EXPECT_FALSE(field.hasDirtyKeys());
}

// ---- out cleared and stats reset ----

std::vector<PlacementHit> sentinels(int count) {
    std::vector<PlacementHit> out;
    for (int i = 0; i < count; ++i) {
        out.push_back({{-1000 - i, -1000 - i}, {-999, -999}});
    }
    return out;
}

bool holdsNoSentinel(const std::vector<PlacementHit> &out) {
    return std::none_of(out.begin(), out.end(), [](const PlacementHit &hit) {
        return hit.chunk_ == ivec2{-999, -999};
    });
}

TEST(QueryPlacementsTest, ClearsTheCallerVectorOnEveryPath) {
    const PlacementField field = makeFreeField();
    PlacementQueryStats stats;

    std::vector<PlacementHit> out = sentinels(3);
    queryPlacements(field, freeParams(), out, &stats);
    EXPECT_GE(out.size(), 1u);
    EXPECT_TRUE(holdsNoSentinel(out));

    out = sentinels(3);
    stats = {7, 7, 7};
    PlacementParams rejected = freeParams();
    rejected.k_ = 0;
    EXPECT_THROW(queryPlacements(field, rejected, out, &stats), std::invalid_argument);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(stats, (PlacementQueryStats{0, 0, 0}));

    const PlacementField occupied = makeOccupiedChunkWithFreeCells({});
    out = sentinels(3);
    queryPlacements(occupied, occupiedChunkParams(), out, &stats);
    EXPECT_TRUE(out.empty());

    PlacementParams params = freeParams();
    out = sentinels(params.k_);
    queryPlacements(field, params, out);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(params.k_));
    EXPECT_TRUE(holdsNoSentinel(out));
}

// ---- bounded at K, sharp at k = 1 ----

TEST(QueryPlacementsTest, NeverExceedsKAndEarlyOutsBeforeTheFirstDraw) {
    const PlacementField field = makeFreeField();
    std::vector<PlacementHit> out;
    PlacementQueryStats stats;

    PlacementParams params = freeParams();
    params.k_ = 6;
    queryPlacements(field, params, out, &stats);
    EXPECT_EQ(out.size(), 6u);

    const PlacementField shortfall = makeOccupiedChunkWithFreeCells({{16, 16}});
    queryPlacements(shortfall, occupiedChunkParams(), out, &stats);
    EXPECT_LE(out.size(), static_cast<std::size_t>(occupiedChunkParams().k_));

    params.k_ = 1;
    queryPlacements(field, params, out, &stats);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], (PlacementHit{params.anchor_, fieldChunkOf(params.anchor_)}));
    EXPECT_EQ(stats.candidatesDrawn_, 0);
}

// ---- determinism with a control ----

TEST(QueryPlacementsTest, SameSeedOnIndependentlyBuiltFieldsIsVectorEqual) {
    const PlacementField left = makeEndToEndField();
    const PlacementField right = makeEndToEndField();
    std::vector<PlacementHit> leftOut;
    std::vector<PlacementHit> rightOut;
    PlacementQueryStats leftStats;
    PlacementQueryStats rightStats;

    queryPlacements(left, endToEndParams(), leftOut, &leftStats);
    queryPlacements(right, endToEndParams(), rightOut, &rightStats);
    EXPECT_EQ(leftOut, rightOut);
    EXPECT_EQ(leftStats, rightStats);

    PlacementParams reseeded = endToEndParams();
    reseeded.seed_ = 54321;
    queryPlacements(right, reseeded, rightOut, &rightStats);
    EXPECT_NE(leftOut, rightOut);
}

// ---- spacing, clearance, occupancy ----

TEST(QueryPlacementsTest, EveryHitHonoursSpacingClearanceAndChunk) {
    const PlacementField field = makeEndToEndField();
    const PlacementParams params = endToEndParams();
    std::vector<PlacementHit> out;
    queryPlacements(field, params, out);
    ASSERT_GE(out.size(), 2u);

    expectPairwiseSpacing(out, params.minSpacing_);
    for (const PlacementHit &hit : out) {
        EXPECT_TRUE(field.clearance().hasClearance(hit.cell_, params.clearance_));
        EXPECT_EQ(hit.chunk_, fieldChunkOf(hit.cell_));
    }
}

TEST(QueryPlacementsTest, ZeroClearanceRelaxesRadiusWithoutAdmittingOccupiedCells) {
    const PlacementField field = makeOccupiedChunkWithFreeCells({{16, 16}});
    PlacementParams params = occupiedChunkParams();
    params.k_ = 1;
    std::vector<PlacementHit> out;

    queryPlacements(field, params, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].cell_, ivec2(16, 16));

    params.anchor_ = {17, 16};
    queryPlacements(field, params, out);
    EXPECT_TRUE(out.empty());
}

// ---- region flag, both arms ----

TEST(QueryPlacementsTest, RegionFlagConfinesHitsToTheAnchorsRegion) {
    const PlacementField field = makeTwoRegionField();
    PlacementParams params = twoRegionParams();
    std::vector<PlacementHit> out;

    queryPlacements(field, params, out);
    ASSERT_GE(out.size(), 2u);
    for (const PlacementHit &hit : out) {
        EXPECT_TRUE(field.regions().sameRegion(hit.cell_, params.anchor_));
    }
    EXPECT_EQ(countFarRegionHits(out), 0);

    params.sameRegionAsAnchor_ = false;
    queryPlacements(field, params, out);
    EXPECT_GT(countFarRegionHits(out), 0);

    params.sameRegionAsAnchor_ = true;
    params.anchor_ = {kWallX, 16};
    queryPlacements(field, params, out);
    EXPECT_TRUE(out.empty());

    params.anchor_ = {6, 200};
    queryPlacements(field, params, out);
    EXPECT_TRUE(out.empty());
}

// ---- K-shortfall ----

TEST(QueryPlacementsTest, ShortfallReturnsExactlyTheReachableValidCount) {
    std::vector<PlacementHit> out;
    PlacementQueryStats stats;

    const PlacementField onlyAnchor = makeOccupiedChunkWithFreeCells({{16, 16}});
    queryPlacements(onlyAnchor, occupiedChunkParams(), out, &stats);
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(stats.candidatesDrawn_, kPlacementAttempts);

    // With minSpacing 1 the annulus is the twelve offsets with 1 <= d^2 <= 4,
    // which includes (-1, 0) and (+1, 0), so both neighbours are one attempt
    // away from the anchor and the anchor is one attempt away from each.
    const PlacementField line = makeOccupiedChunkWithFreeCells({{15, 16}, {16, 16}, {17, 16}});
    queryPlacements(line, occupiedChunkParams(), out, &stats);
    EXPECT_EQ(out.size(), 3u);
}

// ---- pruning fires ----

TEST(QueryPlacementsTest, ChunkSummariesPruneMostOfAMostlyOccupiedField) {
    const PlacementField field = makePruningField();
    ASSERT_EQ(field.occupancy().chunkCount(), static_cast<std::size_t>(kPruningChunkCount));
    std::vector<PlacementHit> out;
    PlacementQueryStats stats;

    queryPlacements(field, pruningParams(), out, &stats);
    EXPECT_GE(out.size(), 1u);
    EXPECT_GT(stats.chunksPruned_, 0);
    EXPECT_LT(stats.chunksConsidered_, kPruningChunkCount);
    EXPECT_GE(stats.chunksConsidered_, 2);
}

// ---- draw order pinned and must-differ ----

TEST(QueryPlacementsTest, DrawOrderIsPinnedAndPolicyVariantsDiffer) {
    const PlacementField field = makeEndToEndField();
    const PlacementParams params = endToEndParams();
    std::vector<PlacementHit> out;
    PlacementQueryStats stats;
    queryPlacements(field, params, out, &stats);

    const std::vector<PlacementHit> expected{
        {{30, 2}, {0, 0}},
        {{30, -5}, {0, -1}},
        {{25, 5}, {0, 0}},
        {{35, 0}, {1, 0}},
        {{27, 12}, {0, 0}},
        {{34, 10}, {1, 0}},
        {{28, 8}, {0, 0}},
        {{33, 5}, {1, 0}},
    };
    const PlacementQueryStats expectedStats{3, 0, 13};
    EXPECT_EQ(out, expected);
    EXPECT_EQ(stats, expectedStats);

    ReferenceDraw reference{field, params};
    reference.run();
    EXPECT_EQ(reference.hits_, expected);
    EXPECT_EQ(reference.stats_, expectedStats);

    ReferenceDraw modulo{field, params, RangeMap::Modulo};
    modulo.run();
    EXPECT_NE(modulo.hits_, expected);

    ReferenceDraw lifo{field, params, RangeMap::MultiplyShift, Selection::Lifo};
    lifo.run();
    EXPECT_NE(lifo.hits_, expected);
}

TEST(QueryPlacementsTest, CandidatesPastInt32AreRejectedWithoutWrapping) {
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    PlacementField field(1);
    field.setCell({kMax, kMax}, 0);
    field.setCell({kMin, kMin}, 0);
    field.update();

    PlacementParams params;
    params.anchor_ = {kMax - 16, kMax - 16};
    params.k_ = 4;
    params.minSpacing_ = 2;
    params.clearance_ = 0;
    params.seed_ = 3;
    std::vector<PlacementHit> out;
    queryPlacements(field, params, out);
    EXPECT_GE(out.size(), 1u);
    for (const PlacementHit &hit : out) {
        EXPECT_EQ(hit.chunk_, fieldChunkOf(params.anchor_));
    }
}

// ---- end-to-end ----

TEST(QueryPlacementsTest, EndToEndReturnsKQualifiedHits) {
    const PlacementField field = makeEndToEndField();
    const PlacementParams params = endToEndParams();
    std::vector<PlacementHit> out;
    queryPlacements(field, params, out);

    ASSERT_EQ(out.size(), static_cast<std::size_t>(params.k_));
    EXPECT_EQ(out[0].cell_, params.anchor_);
    expectPairwiseSpacing(out, params.minSpacing_);
    for (const PlacementHit &hit : out) {
        EXPECT_TRUE(field.clearance().hasClearance(hit.cell_, params.clearance_));
        EXPECT_TRUE(field.regions().sameRegion(hit.cell_, params.anchor_));
        EXPECT_LT(hit.cell_.x, 40);
        EXPECT_EQ(hit.chunk_, fieldChunkOf(hit.cell_));
    }
}

} // namespace
