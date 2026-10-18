#include <gtest/gtest.h>

#include <irreden/world/save_component_inventory.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/audio/save_serializers_audio.hpp>
#include <irreden/common/save_serializers_common.hpp>
#include <irreden/demo/save_serializers_demo.hpp>
#include <irreden/input/save_serializers_input.hpp>
#include <irreden/render/save_serializers_render.hpp>
#include <irreden/update/save_serializers_update.hpp>
#include <irreden/voxel/save_serializers_voxel.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_entity.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Per-component SaveSerialize<C> coverage for hand-written layouts, plus the
// membership assertion
// that proves that registry is *derived* from the inventory rather than
// hand-curated.
//
// Two shapes of assertion per serializer:
//
//   - `roundTrip(value)` returns the value after a write -> read cycle, so a
//     test can assert on the specific fields that matter.
//   - `reserializesIdentically(value)` writes, reads, and writes again,
//     asserting the two byte images match. That catches a field written but
//     never read back (or read in the wrong order) *without* the test needing
//     access to the component's fields — which is the only practical check
//     for C_TriangleCanvasBackground, whose state is entirely private.
//
// Edge cases the plan calls out — empty string, empty vector, and
// C_BindPoints' map-order determinism — get their own tests below.

namespace {

using namespace IRComponents;

template <typename C>
concept UsesRawImageSerializer =
    requires { IRWorld::SaveSerialize<C>::kRawImage; } && IRWorld::SaveSerialize<C>::kRawImage;

static_assert(IRWorld::SaveTrait<C_EntityCanvas>::kHandWrittenLayout);
static_assert(IRWorld::SaveTrait<C_HitBox2D>::kHandWrittenLayout);
static_assert(IRWorld::SaveTrait<C_ShapeDescriptor>::kHandWrittenLayout);
static_assert(IRWorld::SaveTrait<C_TextStyle>::kHandWrittenLayout);
static_assert(!UsesRawImageSerializer<C_EntityCanvas>);
static_assert(!UsesRawImageSerializer<C_HitBox2D>);
static_assert(!UsesRawImageSerializer<C_ShapeDescriptor>);
static_assert(!UsesRawImageSerializer<C_TextStyle>);

template <typename C> std::vector<std::uint8_t> serialize(const C &value) {
    IRAsset::MemoryBinaryWriter writer;
    IRWorld::SaveSerialize<C>::write(writer, value);
    return writer.buffer();
}

// Write -> read. Fails the calling test (via ADD_FAILURE) and returns a
// default-ish value if the read reports an error.
template <typename C> IRAsset::Result<C> deserialize(const std::vector<std::uint8_t> &bytes) {
    IRAsset::MemoryBinaryReader reader(bytes.data(), bytes.size());
    return IRWorld::SaveSerialize<C>::read(reader);
}

// A serializer must consume exactly what it produced: a read that stops short
// would silently corrupt the *next* column in a real ARCH chunk, where
// component rows are written back to back with no per-row framing.
template <typename C> void expectConsumesAllBytes(const C &value) {
    const std::vector<std::uint8_t> bytes = serialize(value);
    IRAsset::MemoryBinaryReader reader(bytes.data(), bytes.size());
    IRAsset::Result<C> restored = IRWorld::SaveSerialize<C>::read(reader);
    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(reader.remaining(), 0u)
        << "serializer left " << reader.remaining() << " unread byte(s)";
}

template <typename C> void expectReserializesIdentically(const C &value) {
    const std::vector<std::uint8_t> first = serialize(value);
    IRAsset::Result<C> restored = deserialize<C>(first);
    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    const std::vector<std::uint8_t> second = serialize(restored.value_);
    EXPECT_EQ(first, second) << "re-serializing the restored value produced different bytes — a "
                                "field is written but not read back, or read out of order";
}

// Convenience for the common "round-trip and inspect" case.
template <typename C> C roundTrip(const C &value) {
    IRAsset::Result<C> restored = deserialize<C>(serialize(value));
    EXPECT_TRUE(restored.ok()) << restored.status_.message_;
    return restored.value_;
}

// Rule #5: a truncated payload is a recoverable error, not a crash or a
// silently half-filled component. Checked on a representative serializer from
// each container shape (string, string vector, trivial vector, map).
template <typename C> void expectTruncationIsRecoverable(const C &value) {
    const std::vector<std::uint8_t> full = serialize(value);
    ASSERT_GT(full.size(), 1u);
    for (std::size_t cut = 0; cut < full.size(); ++cut) {
        const std::vector<std::uint8_t> partial(full.begin(), full.begin() + cut);
        IRAsset::MemoryBinaryReader reader(partial.data(), partial.size());
        IRAsset::Result<C> restored = IRWorld::SaveSerialize<C>::read(reader);
        EXPECT_FALSE(restored.ok()) << "truncating to " << cut << " byte(s) still read as OK";
    }
}

// The single retired-version reader `C` declares for @p fromVersion, run over
// @p bytes. Fails the calling test if the reader is missing or leaves bytes.
template <typename C>
IRAsset::Result<C> migrate(std::uint32_t fromVersion, const std::vector<std::uint8_t> &bytes) {
    const auto migrators = IRWorld::SaveMigration<C>::migrators();
    const auto migrator = std::find_if(migrators.begin(), migrators.end(), [&](const auto &entry) {
        return entry.first == fromVersion;
    });
    if (migrator == migrators.end()) {
        ADD_FAILURE() << "no migrator for version " << fromVersion;
        return IRAsset::Result<C>::error(IRAsset::BinaryIOError::MigratorMissing, "missing");
    }
    IRAsset::MemoryBinaryReader reader(bytes.data(), bytes.size());
    IRAsset::Result<C> restored = migrator->second(reader);
    EXPECT_EQ(reader.remaining(), 0u) << "migrator left unread bytes";
    return restored;
}

// The current record of a text-bearing component ends in its text-size byte;
// overwriting it with @p raw must make the reader refuse the record.
template <typename C> void expectOutOfRangeTextSizeIsRejected(const C &value, std::uint8_t raw) {
    std::vector<std::uint8_t> bytes = serialize(value);
    ASSERT_FALSE(bytes.empty());
    bytes.back() = raw;
    const IRAsset::Result<C> restored = deserialize<C>(bytes);
    ASSERT_FALSE(restored.ok()) << "text size " << int{raw} << " read back as valid";
    EXPECT_EQ(restored.status_.code_, IRAsset::BinaryIOError::UnknownTag);
    EXPECT_NE(restored.status_.message_.find("text size"), std::string::npos)
        << restored.status_.message_;
}

// --- common/ ---------------------------------------------------------------

TEST(SaveSerializers, NameRoundTrips) {
    EXPECT_EQ(roundTrip(C_Name{"player one"}).name_, "player one");
    expectReserializesIdentically(C_Name{"player one"});
    expectConsumesAllBytes(C_Name{"player one"});
}

TEST(SaveSerializers, NameRoundTripsEmptyString) {
    EXPECT_EQ(roundTrip(C_Name{""}).name_, "");
    expectConsumesAllBytes(C_Name{""});
}

TEST(SaveSerializers, TimerRoundTrips) {
    C_Timer timer{"spawn", 1200, 300};
    timer.startTick_ = 900;
    timer.active_ = false;
    timer.fired_ = true;

    const C_Timer restored = roundTrip(timer);
    EXPECT_EQ(restored.name_, "spawn");
    EXPECT_EQ(restored.startTick_, 900u);
    EXPECT_EQ(restored.targetTick_, 1200u);
    EXPECT_EQ(restored.intervalTicks_, 300u);
    EXPECT_FALSE(restored.active_);
    EXPECT_TRUE(restored.fired_);
    expectConsumesAllBytes(timer);
}

TEST(SaveSerializers, StopwatchRoundTrips) {
    C_Stopwatch stopwatch{"lap", 4242};
    stopwatch.pausedElapsed_ = 77;
    stopwatch.running_ = false;

    const C_Stopwatch restored = roundTrip(stopwatch);
    EXPECT_EQ(restored.name_, "lap");
    EXPECT_EQ(restored.startTick_, 4242u);
    EXPECT_EQ(restored.pausedElapsed_, 77u);
    EXPECT_FALSE(restored.running_);
    expectConsumesAllBytes(stopwatch);
}

TEST(SaveSerializers, CycleRoundTripsWithBreakpoints) {
    C_Cycle cycle{"day", 1000, 25};
    cycle.addBreakpoint(0.25f);
    cycle.addBreakpoint(0.5f);
    cycle.addBreakpoint(0.75f);
    ASSERT_EQ(cycle.numBreakpoints_, 3);
    cycle.lastCycleNum_ = 9;
    cycle.lastSegmentIndex_ = 2;
    cycle.boundaryCrossed_ = true;
    cycle.fromCycle_ = 8;
    cycle.toCycle_ = 9;
    cycle.segmentIndex_ = 2;
    cycle.fromSegment_ = 1;
    cycle.toSegment_ = 2;

    const C_Cycle restored = roundTrip(cycle);
    EXPECT_EQ(restored.name_, "day");
    EXPECT_EQ(restored.periodTicks_, 1000u);
    EXPECT_EQ(restored.phaseOffset_, 25u);
    ASSERT_EQ(restored.numBreakpoints_, 3);
    EXPECT_EQ(restored.breakpoints_[0], 250u);
    EXPECT_EQ(restored.breakpoints_[1], 500u);
    EXPECT_EQ(restored.breakpoints_[2], 750u);
    EXPECT_EQ(restored.lastCycleNum_, 9u);
    EXPECT_TRUE(restored.boundaryCrossed_);
    EXPECT_EQ(restored.toSegment_, 2);
    expectConsumesAllBytes(cycle);
}

TEST(SaveSerializers, CycleRoundTripsWithNoBreakpoints) {
    const C_Cycle cycle{"bare", 60};
    const C_Cycle restored = roundTrip(cycle);
    EXPECT_EQ(restored.numBreakpoints_, 0);
    EXPECT_EQ(restored.lastCycleNum_, C_Cycle::kUnprimed);
    expectConsumesAllBytes(cycle);
}

// A breakpoint count above this build's inline-array capacity must be
// rejected rather than copied past the end of the array.
TEST(SaveSerializers, CycleRejectsOverlongBreakpointCount) {
    std::vector<std::uint8_t> bytes = serialize(C_Cycle{"bad", 100});
    // Layout: string("bad") = varuint len + 3 bytes, then two u64, then the
    // breakpoint count byte.
    const std::size_t countIndex = 1 + 3 + 8 + 8;
    ASSERT_LT(countIndex, bytes.size());
    ASSERT_EQ(bytes[countIndex], 0u) << "expected the breakpoint count at this offset";
    bytes[countIndex] = C_Cycle::kMaxBreakpoints + 1;

    IRAsset::Result<C_Cycle> restored = deserialize<C_Cycle>(bytes);
    EXPECT_FALSE(restored.ok());
    EXPECT_EQ(restored.status_.code_, IRAsset::BinaryIOError::UnknownTag);
}

// --- demo/ -----------------------------------------------------------------

TEST(SaveSerializers, ExampleRoundTrips) {
    EXPECT_EQ(roundTrip(C_Example{"hello"}).exampleSentence_, "hello");
    expectConsumesAllBytes(C_Example{"hello"});
}

// --- render/ ---------------------------------------------------------------

TEST(SaveSerializers, EntityCanvasV1MigrationPreservesAuthoredStateAndDefaultsFogState) {
    IRWorld::detail::EntityCanvasV1 old{
        42,
        IRMath::ivec2(320, 180),
        false,
        true,
        7,
    };
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));

    const auto migrators = IRWorld::SaveMigration<C_EntityCanvas>::migrators();
    ASSERT_EQ(migrators.size(), 2u);
    const auto migrator = std::find_if(migrators.begin(), migrators.end(), [](const auto &entry) {
        return entry.first == 1u;
    });
    ASSERT_NE(migrator, migrators.end());
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "canvas-v1");
    const IRAsset::Result<C_EntityCanvas> restored = migrator->second(reader);

    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(restored.value_.canvasEntity_, old.canvasEntity_);
    EXPECT_EQ(restored.value_.canvasSize_, old.canvasSize_);
    EXPECT_EQ(restored.value_.visible_, old.visible_);
    EXPECT_EQ(restored.value_.screenLocked_, old.screenLocked_);
    EXPECT_EQ(restored.value_.depthPriority_, old.depthPriority_);
    EXPECT_FLOAT_EQ(restored.value_.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(restored.value_.fogHidden_);
    EXPECT_EQ(reader.remaining(), 0u);
    EXPECT_EQ(IRWorld::saveVersion<C_EntityCanvas>(), 3u);
}

TEST(SaveSerializers, EntityCanvasRoundTripPreservesAuthoredStateAndDefaultsFogState) {
    C_EntityCanvas canvas{42, IRMath::ivec2(320, 180), false, true, 7};
    canvas.fogRevealFactor_ = 0.0f;
    canvas.fogHidden_ = true;

    const std::vector<std::uint8_t> bytes = serialize(canvas);
    ASSERT_EQ(bytes.size(), 22u);
    const C_EntityCanvas restored = roundTrip(canvas);

    EXPECT_EQ(restored.canvasEntity_, canvas.canvasEntity_);
    EXPECT_EQ(restored.canvasSize_, canvas.canvasSize_);
    EXPECT_EQ(restored.visible_, canvas.visible_);
    EXPECT_EQ(restored.screenLocked_, canvas.screenLocked_);
    EXPECT_EQ(restored.depthPriority_, canvas.depthPriority_);
    EXPECT_FLOAT_EQ(restored.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(restored.fogHidden_);
    expectConsumesAllBytes(canvas);
    expectReserializesIdentically(canvas);
}

TEST(SaveSerializers, EntityCanvasV2MigrationPreservesAuthoredStateAndDefaultsFogState) {
    IRWorld::detail::EntityCanvasV2 old{
        42,
        IRMath::ivec2(320, 180),
        false,
        true,
        7,
        0.0f,
        true,
    };
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));

    const auto migrators = IRWorld::SaveMigration<C_EntityCanvas>::migrators();
    ASSERT_EQ(migrators.size(), 2u);
    const auto migrator = std::find_if(migrators.begin(), migrators.end(), [](const auto &entry) {
        return entry.first == 2u;
    });
    ASSERT_NE(migrator, migrators.end());
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "canvas-v2");
    const IRAsset::Result<C_EntityCanvas> restored = migrator->second(reader);

    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(restored.value_.canvasEntity_, old.canvasEntity_);
    EXPECT_EQ(restored.value_.canvasSize_, old.canvasSize_);
    EXPECT_EQ(restored.value_.visible_, old.visible_);
    EXPECT_EQ(restored.value_.screenLocked_, old.screenLocked_);
    EXPECT_EQ(restored.value_.depthPriority_, old.depthPriority_);
    EXPECT_FLOAT_EQ(restored.value_.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(restored.value_.fogHidden_);
    EXPECT_FALSE(restored.value_.fogGhost_);
    EXPECT_EQ(reader.remaining(), 0u);
}

TEST(SaveSerializers, EntityCanvasRoundTripClearsTransientFogGhostVerdict) {
    C_EntityCanvas canvas{};
    canvas.canvasEntity_ = 42;
    canvas.fogGhost_ = true;

    const C_EntityCanvas restored = roundTrip(canvas);

    EXPECT_EQ(restored.canvasEntity_, canvas.canvasEntity_);
    EXPECT_FALSE(restored.fogGhost_);
    expectConsumesAllBytes(canvas);
}

// A version-1 camera row is the raw image of an empty struct: one byte with no
// defined value. Set here to the value that would read as "policy on" if the
// migrator trusted it.
TEST(SaveSerializers, CameraV1MigrationRestoresTheSnappedZoomPolicy) {
    const std::uint8_t undefinedMarkerByte = 0xFF;
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&undefinedMarkerByte, sizeof(undefinedMarkerByte));

    const auto migrators = IRWorld::SaveMigration<C_Camera>::migrators();
    ASSERT_EQ(migrators.size(), 1u);
    ASSERT_EQ(migrators.front().first, 1u);
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "camera-v1");
    const IRAsset::Result<C_Camera> restored = migrators.front().second(reader);

    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_FALSE(restored.value_.continuousZoom_);
    EXPECT_EQ(reader.remaining(), 0u);
    EXPECT_EQ(IRWorld::saveVersion<C_Camera>(), 2u);
}

TEST(SaveSerializers, CameraZoomPolicyRoundTrips) {
    C_Camera continuous;
    continuous.continuousZoom_ = true;
    EXPECT_TRUE(roundTrip(continuous).continuousZoom_);
    EXPECT_FALSE(roundTrip(C_Camera{}).continuousZoom_);
    expectReserializesIdentically(continuous);
    expectConsumesAllBytes(continuous);
}

TEST(SaveSerializers, GizmoHandleV1MigrationPreservesEveryFieldAndDefaultsViewClearance) {
    IRWorld::detail::GizmoHandleV1 old{
        GizmoKind::ROTATE_RING,
        GizmoAxis::Y,
        true,
        false,
        IRMath::vec4(5.0f, 0.4f, 0.25f, 0.125f),
        IRMath::vec3(1.5f, -2.5f, 3.5f),
        IRMath::Color{60, 200, 61, 254},
        77,
    };
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));

    const auto migrators = IRWorld::SaveMigration<C_GizmoHandle>::migrators();
    ASSERT_EQ(migrators.size(), 1u);
    ASSERT_EQ(migrators.front().first, 1u);
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "gizmo-v1");
    const IRAsset::Result<C_GizmoHandle> restored = migrators.front().second(reader);

    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(restored.value_.kind_, old.kind_);
    EXPECT_EQ(restored.value_.axis_, old.axis_);
    EXPECT_EQ(restored.value_.hover_, old.hover_);
    EXPECT_EQ(restored.value_.isAnchor_, old.isAnchor_);
    EXPECT_EQ(restored.value_.referenceParams_, old.referenceParams_);
    EXPECT_EQ(restored.value_.referenceLocalPos_, old.referenceLocalPos_);
    EXPECT_EQ(restored.value_.baseColor_.red_, old.baseColor_.red_);
    EXPECT_EQ(restored.value_.baseColor_.green_, old.baseColor_.green_);
    EXPECT_EQ(restored.value_.baseColor_.blue_, old.baseColor_.blue_);
    EXPECT_EQ(restored.value_.baseColor_.alpha_, old.baseColor_.alpha_);
    EXPECT_EQ(restored.value_.anchorEntity_, old.anchorEntity_);
    EXPECT_FLOAT_EQ(restored.value_.viewClearance_, 0.0f);
    EXPECT_EQ(reader.remaining(), 0u);
    EXPECT_EQ(IRWorld::saveVersion<C_GizmoHandle>(), 2u);
}

TEST(SaveSerializers, GizmoHandleViewClearanceRoundTrips) {
    C_GizmoHandle handle{GizmoKind::TRANSLATE_ARROW, GizmoAxis::Z};
    handle.viewClearance_ = 13.75f;
    EXPECT_FLOAT_EQ(roundTrip(handle).viewClearance_, 13.75f);
    expectConsumesAllBytes(handle);
}

// The carried raster phase is runtime state of the frame loop. Saving it
// would resume a history the loaded world never rendered.
TEST(SaveSerializers, CameraZoomFrameStateStaysOptedOut) {
    EXPECT_FALSE(IRWorld::shouldSave<C_CameraZoomFrameState>());
}

// --- voxel/ ----------------------------------------------------------------

TEST(SaveSerializers, JointNameRoundTrips) {
    C_JointName joint{};
    joint.name_ = "spine.02";
    EXPECT_EQ(roundTrip(joint).name_, "spine.02");
    expectConsumesAllBytes(joint);
}

TEST(SaveSerializers, ShapeDescriptorV1MigrationPreservesLayoutAndDefaultsFogFactor) {
    IRWorld::detail::ShapeDescriptorV1 old{
        IRMath::SDF::ShapeType::SPHERE,
        IRMath::vec4(7.0f, 6.0f, 5.0f, 4.0f),
        IRMath::Color{1, 2, 3, 4},
        IRMath::SDF::SHAPE_FLAG_VISIBLE | IRMath::SDF::SHAPE_FLAG_FOG_BODY,
        IRRender::LodLevel::LOD_3,
        IRRender::LodLevel::LOD_1,
        42,
    };
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));

    const auto migrators = IRWorld::SaveMigration<C_ShapeDescriptor>::migrators();
    ASSERT_EQ(migrators.size(), 1u);
    ASSERT_EQ(migrators.front().first, 1u);
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "shape-v1");
    const IRAsset::Result<C_ShapeDescriptor> restored = migrators.front().second(reader);

    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(restored.value_.shapeType_, old.shapeType_);
    EXPECT_EQ(restored.value_.params_, old.params_);
    EXPECT_EQ(restored.value_.color_.toPackedRGBA(), old.color_.toPackedRGBA());
    EXPECT_EQ(restored.value_.flags_, old.flags_);
    EXPECT_EQ(restored.value_.fogBodyFactor_, 255u);
    EXPECT_EQ(restored.value_.lodMin_, old.lodMin_);
    EXPECT_EQ(restored.value_.lodMax_, old.lodMax_);
    EXPECT_EQ(restored.value_.canvasEntity_, old.canvasEntity_);
    EXPECT_EQ(reader.remaining(), 0u);
    EXPECT_EQ(IRWorld::saveVersion<C_ShapeDescriptor>(), 2u);
}

TEST(SaveSerializers, ShapeDescriptorRoundTripClearsTransientFogVerdicts) {
    C_ShapeDescriptor shape{};
    shape.flags_ |= IRMath::SDF::SHAPE_FLAG_FOG_BODY | IRMath::SDF::SHAPE_FLAG_FOG_HIDDEN |
                    IRMath::SDF::SHAPE_FLAG_FOG_GHOST;
    shape.fogBodyFactor_ = 73u;

    const C_ShapeDescriptor restored = roundTrip(shape);

    EXPECT_NE(restored.flags_ & IRMath::SDF::SHAPE_FLAG_FOG_BODY, 0u);
    EXPECT_EQ(restored.flags_ & IRMath::SDF::SHAPE_FLAG_FOG_HIDDEN, 0u);
    EXPECT_EQ(restored.flags_ & IRMath::SDF::SHAPE_FLAG_FOG_GHOST, 0u);
    EXPECT_EQ(restored.fogBodyFactor_, 73u);
    expectConsumesAllBytes(shape);
}

TEST(SaveSerializers, SkeletonRoundTrips) {
    C_Skeleton skeleton{};
    // A severance hole (kNullEntity) in the middle: slot order is the bone-id
    // space, so the hole must survive rather than being compacted away.
    skeleton.joints_ = {11, IREntity::kNullEntity, 13};
    skeleton.bindPose_.resize(3);
    skeleton.bindPose_[0].translation_ = IRMath::vec3(1.0f, 2.0f, 3.0f);
    skeleton.bindPose_[2].translation_ = IRMath::vec3(-4.0f, 5.0f, -6.0f);

    const C_Skeleton restored = roundTrip(skeleton);
    ASSERT_EQ(restored.joints_.size(), 3u);
    EXPECT_EQ(restored.joints_[0], 11u);
    EXPECT_EQ(restored.joints_[1], IREntity::kNullEntity);
    EXPECT_EQ(restored.joints_[2], 13u);
    ASSERT_EQ(restored.bindPose_.size(), 3u);
    EXPECT_FLOAT_EQ(restored.bindPose_[0].translation_.x, 1.0f);
    EXPECT_FLOAT_EQ(restored.bindPose_[2].translation_.z, -6.0f);
    expectConsumesAllBytes(skeleton);
}

TEST(SaveSerializers, SkeletonRoundTripsEmpty) {
    const C_Skeleton skeleton{};
    const C_Skeleton restored = roundTrip(skeleton);
    EXPECT_TRUE(restored.joints_.empty());
    EXPECT_TRUE(restored.bindPose_.empty());
    expectConsumesAllBytes(skeleton);
}

// A rig with joints but no bind pose: the two vectors are independent, so a
// serializer that assumed them parallel would corrupt the read.
TEST(SaveSerializers, SkeletonRoundTripsJointsWithoutBindPose) {
    C_Skeleton skeleton{};
    skeleton.joints_ = {1, 2, 3, 4};
    const C_Skeleton restored = roundTrip(skeleton);
    EXPECT_EQ(restored.joints_.size(), 4u);
    EXPECT_TRUE(restored.bindPose_.empty());
    expectConsumesAllBytes(skeleton);
}

TEST(SaveSerializers, BindPointsRoundTrips) {
    C_BindPoints points{};
    points.setPoint(
        "hand.L",
        BindPointRuntime{3, IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)}
    );
    points.setPoint(
        "hand.R",
        BindPointRuntime{4, IRMath::vec3(-1.0f, 0.0f, 0.0f), IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)}
    );

    const C_BindPoints restored = roundTrip(points);
    ASSERT_TRUE(restored.hasPoint("hand.L"));
    ASSERT_TRUE(restored.hasPoint("hand.R"));
    EXPECT_EQ(restored.points_.at("hand.L").boneId_, 3u);
    EXPECT_FLOAT_EQ(restored.points_.at("hand.R").offset_.x, -1.0f);
    expectConsumesAllBytes(points);
}

// The map is an unordered_map, so hash order is not a contract. Two maps with
// the same entries inserted in opposite orders must serialize to identical
// bytes — otherwise a same-world double-save is not byte-identical
// (world-snapshot criterion 6).
TEST(SaveSerializers, BindPointsAreOrderIndependent) {
    const BindPointRuntime a{
        1,
        IRMath::vec3(1.0f, 2.0f, 3.0f),
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    };
    const BindPointRuntime b{
        2,
        IRMath::vec3(4.0f, 5.0f, 6.0f),
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    };
    const BindPointRuntime c{
        3,
        IRMath::vec3(7.0f, 8.0f, 9.0f),
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    };

    C_BindPoints forward{};
    forward.setPoint("alpha", a);
    forward.setPoint("beta", b);
    forward.setPoint("gamma", c);

    C_BindPoints reverse{};
    reverse.setPoint("gamma", c);
    reverse.setPoint("beta", b);
    reverse.setPoint("alpha", a);

    EXPECT_EQ(serialize(forward), serialize(reverse))
        << "bind-point bytes depend on unordered_map iteration order";
}

TEST(SaveSerializers, BindPointsRoundTripsEmpty) {
    const C_BindPoints points{};
    EXPECT_TRUE(roundTrip(points).points_.empty());
    expectConsumesAllBytes(points);
}

// --- audio/ ----------------------------------------------------------------

TEST(SaveSerializers, MidiSequenceRoundTrips) {
    C_MidiSequence sequence{140.0f, 3, 4, 2, false};
    sequence.insertNote(0.0, 0.25, 60, 100);
    sequence.insertNote(1.0, 0.5, 64, 90);
    ASSERT_EQ(sequence.messageSequence_.size(), 4u);
    sequence.tickCount_ = 512.0;
    sequence.nextMessageIndex_ = 2;

    const C_MidiSequence restored = roundTrip(sequence);
    EXPECT_FLOAT_EQ(restored.bpm_, 140.0f);
    EXPECT_EQ(restored.timeSignature_.first, 3);
    EXPECT_EQ(restored.timeSignature_.second, 4);
    EXPECT_EQ(restored.lengthMeasures_, 2);
    EXPECT_FALSE(restored.looping_);
    ASSERT_EQ(restored.messageSequence_.size(), 4u);
    EXPECT_EQ(restored.messageSequence_[0].first, sequence.messageSequence_[0].first);
    EXPECT_EQ(restored.messageSequence_[0].second.data1_, 60);
    EXPECT_EQ(restored.messageSequence_[2].second.data1_, 64);
    EXPECT_DOUBLE_EQ(restored.tickCount_, 512.0);
    EXPECT_EQ(restored.nextMessageIndex_, 2);
    // The constructor-derived scalars must come back consistent with the
    // authored tempo rather than being persisted independently.
    EXPECT_DOUBLE_EQ(restored.lengthMidiTicks_, sequence.lengthMidiTicks_);
    EXPECT_DOUBLE_EQ(restored.calcMidiTicksPerFrameTick(), sequence.calcMidiTicksPerFrameTick());
    expectConsumesAllBytes(sequence);
}

TEST(SaveSerializers, MidiSequenceRoundTripsEmptySequence) {
    const C_MidiSequence sequence{};
    const C_MidiSequence restored = roundTrip(sequence);
    EXPECT_TRUE(restored.messageSequence_.empty());
    expectConsumesAllBytes(sequence);
}

// --- update/ ---------------------------------------------------------------

TEST(SaveSerializers, PeriodicIdleRoundTrips) {
    C_PeriodicIdle idle{IRMath::vec3{0.0f, 0.0f, 4.0f}, 2.0f, 0.5f};
    idle.addStagePeriodRange(0.0f, 3.0f, 0.0f, 1.0f, IREasingFunctions::kCubicEaseOut);
    idle.appendStageFillEnd(1.0f, 0.0f, IREasingFunctions::kCubicEaseIn);
    ASSERT_EQ(idle.stages_.size(), 2u);
    idle.tickCount_ = 31;
    idle.currentStageIndex_ = 1;
    idle.pauseRequested_ = true;
    idle.resumeCountdownSec_ = 0.75f;

    const C_PeriodicIdle restored = roundTrip(idle);
    EXPECT_EQ(restored.tickCount_, 31);
    EXPECT_FLOAT_EQ(restored.angle_, idle.angle_);
    EXPECT_FLOAT_EQ(restored.amplitude_.z, 4.0f);
    EXPECT_FLOAT_EQ(restored.periodLengthSeconds_, 2.0f);
    ASSERT_EQ(restored.stages_.size(), 2u);
    EXPECT_EQ(restored.stages_[0].easingFunction_, IREasingFunctions::kCubicEaseOut);
    EXPECT_FLOAT_EQ(restored.stages_[1].endAngle_, idle.stages_[1].endAngle_);
    EXPECT_EQ(restored.currentStageIndex_, 1);
    EXPECT_TRUE(restored.isPauseRequested());
    EXPECT_FLOAT_EQ(restored.resumeCountdownSec_, 0.75f);
    // The private per-tick cache is derived, so the check that it came back
    // consistent is that the restored component evaluates to the same offset
    // the live one does at the same angle.
    EXPECT_FLOAT_EQ(restored.valueAtAngle(idle.angle_).z, idle.valueAtAngle(idle.angle_).z);
    expectConsumesAllBytes(idle);
}

TEST(SaveSerializers, PeriodicIdleRoundTripsWithNoStages) {
    const C_PeriodicIdle idle{IRMath::vec3{1.0f, 0.0f, 0.0f}, 1.0f};
    const C_PeriodicIdle restored = roundTrip(idle);
    EXPECT_TRUE(restored.stages_.empty());
    expectConsumesAllBytes(idle);
}

// C_GotoEasing3D and C_RotationTarget store their easing as the
// IREasingFunctions enum, so both are trivially copyable and go through the
// raw-image arm rather than an explicit SaveSerialize<C>. Both tests assert a
// *non-default* enum: kLinearInterpolation is the constructor default, so a
// reader that substituted it would still satisfy a weaker check.

TEST(SaveSerializers, GotoEasing3DRoundTripsTheAuthoredCurve) {
    C_GotoEasing3D gotoComp{
        IRMath::vec3{1.0f, 2.0f, 3.0f},
        IRMath::vec3{4.0f, 5.0f, 6.0f},
        2.0f,
        IRMath::kBounceEaseOut
    };
    gotoComp.currentFrame_ = 17;

    const C_GotoEasing3D restored = roundTrip(gotoComp);
    EXPECT_EQ(restored.easingFunction_, IRMath::kBounceEaseOut);
    EXPECT_EQ(restored.startPos_, gotoComp.startPos_);
    EXPECT_EQ(restored.endPos_, gotoComp.endPos_);
    EXPECT_EQ(restored.durationFrames_, gotoComp.durationFrames_);
    EXPECT_EQ(restored.currentFrame_, 17);
    EXPECT_FALSE(restored.done_);

    // Raw-image serialization includes padding that memberwise copies need not preserve.
    expectConsumesAllBytes(gotoComp);
}

TEST(SaveSerializers, RotationTargetRoundTripsTheAuthoredCurve) {
    const C_RotationTarget target{
        IRMath::vec3{0.0f, 1.0f, 0.0f},
        0.0f,
        IRMath::kHalfPi,
        0.25f,
        0.0f,
        1.0f,
        IRMath::kQuadraticEaseIn
    };

    const C_RotationTarget restored = roundTrip(target);
    EXPECT_EQ(restored.easingFunction_, IRMath::kQuadraticEaseIn);
    EXPECT_EQ(restored.axis_, target.axis_);
    EXPECT_FLOAT_EQ(restored.minAngle_, 0.0f);
    EXPECT_FLOAT_EQ(restored.maxAngle_, IRMath::kHalfPi);
    EXPECT_FLOAT_EQ(restored.input_, 0.25f);
    EXPECT_FLOAT_EQ(restored.inputMin_, 0.0f);
    EXPECT_FLOAT_EQ(restored.inputMax_, 1.0f);

    expectConsumesAllBytes(target);
    expectReserializesIdentically(target);
}

// --- input/ ----------------------------------------------------------------

TEST(SaveSerializers, HitBox2DRoundTripsAuthoredStateOnly) {
    static_assert(std::is_trivially_copyable_v<C_HitBox2D>);
    static_assert(std::is_trivially_destructible_v<C_HitBox2D>);

    C_HitBox2D hitbox{18.0f, 10.0f};
    hitbox.padding_ = 7.0f;
    hitbox.enabled_ = false;
    hitbox.pickPriority_ = 23;
    hitbox.hovered_ = true;
    hitbox.screenSpaceCenter_ = true;
    hitbox.screenSpacePlaced_ = true;
    hitbox.centerScreen_ = IRMath::vec2{100.0f, 200.0f};
    hitbox.isoDepth_ = 41;

    const C_HitBox2D restored = roundTrip(hitbox);
    EXPECT_EQ(restored.halfExtent_, IRMath::vec2(9.0f, 5.0f));
    EXPECT_FLOAT_EQ(restored.padding_, 7.0f);
    EXPECT_FALSE(restored.enabled_);
    EXPECT_EQ(restored.pickPriority_, 23);
    EXPECT_FALSE(restored.hovered_);
    EXPECT_FALSE(restored.screenSpaceCenter_);
    EXPECT_FALSE(restored.screenSpacePlaced_);
    EXPECT_EQ(restored.centerScreen_, IRMath::vec2(0.0f));
    EXPECT_EQ(restored.isoDepth_, 0);
    expectConsumesAllBytes(hitbox);
    expectReserializesIdentically(hitbox);
}

// --- render/ ---------------------------------------------------------------

TEST(SaveSerializers, TextSegmentRoundTrips) {
    EXPECT_EQ(roundTrip(C_TextSegment{"score: 42"}).text_, "score: 42");
    expectConsumesAllBytes(C_TextSegment{""});
}

TEST(SaveSerializers, TextStyleRoundTripsEveryFieldWithTheSmallSet) {
    const C_TextStyle style{
        IRMath::Color{10, 20, 30, 40},
        -1,
        TextAlignH::RIGHT,
        TextAlignV::BOTTOM,
        96,
        48,
        3,
        IRRender::TextSize::SMALL,
    };
    const C_TextStyle restored = roundTrip(style);
    EXPECT_EQ(restored.color_.red_, 10);
    EXPECT_EQ(restored.color_.green_, 20);
    EXPECT_EQ(restored.color_.blue_, 30);
    EXPECT_EQ(restored.color_.alpha_, 40);
    EXPECT_EQ(restored.wrapWidth_, -1);
    EXPECT_EQ(restored.alignH_, TextAlignH::RIGHT);
    EXPECT_EQ(restored.alignV_, TextAlignV::BOTTOM);
    EXPECT_EQ(restored.boxWidth_, 96);
    EXPECT_EQ(restored.boxHeight_, 48);
    EXPECT_EQ(restored.fontSize_, 3);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    EXPECT_EQ(roundTrip(C_TextStyle{}).textSize_, IRRender::TextSize::LARGE);
    expectConsumesAllBytes(style);
    expectReserializesIdentically(style);
    expectTruncationIsRecoverable(style);
    expectOutOfRangeTextSizeIsRejected(style, 2);
    expectOutOfRangeTextSizeIsRejected(style, 0xFF);
}

TEST(SaveSerializers, TextStyleRejectsAnOutOfRangeAlignment) {
    const C_TextStyle
        style{IRMath::IRColors::kWhite, 0, TextAlignH::CENTER, TextAlignV::CENTER, 0, 0};
    // Color (4 bytes) and wrap width (4) precede the two alignment bytes.
    for (const std::size_t offset : {std::size_t{8}, std::size_t{9}}) {
        std::vector<std::uint8_t> bytes = serialize(style);
        ASSERT_EQ(bytes[offset], 1u);
        bytes[offset] = 3;
        const IRAsset::Result<C_TextStyle> restored = deserialize<C_TextStyle>(bytes);
        ASSERT_FALSE(restored.ok()) << "alignment byte " << offset << " = 3 read back as valid";
        EXPECT_NE(restored.status_.message_.find("alignment"), std::string::npos)
            << restored.status_.message_;
    }
}

#ifndef IR_RELEASE
// The writer's debug mirror of the readers' range checks: the culprit entity is
// still live when the save is taken, where the read failure surfaces a load later.
TEST(SaveSerializers, SavingAnOutOfRangeTextEnumAsserts) {
    C_WidgetLabel label{};
    label.textSize_ = static_cast<IRRender::TextSize>(7);
    IRAsset::MemoryBinaryWriter labelWriter;
    EXPECT_THROW(
        IRWorld::SaveSerialize<C_WidgetLabel>::write(labelWriter, label),
        std::runtime_error
    );

    C_TextStyle style{};
    style.alignV_ = static_cast<TextAlignV>(4);
    IRAsset::MemoryBinaryWriter styleWriter;
    EXPECT_THROW(
        IRWorld::SaveSerialize<C_TextStyle>::write(styleWriter, style),
        std::runtime_error
    );

    IRAsset::MemoryBinaryWriter inRange;
    EXPECT_NO_THROW(IRWorld::SaveSerialize<C_TextStyle>::write(inRange, C_TextStyle{}));
}
#endif

TEST(SaveSerializers, TextStyleV1MigrationPreservesEveryFieldAndReadsLarge) {
    const IRWorld::detail::TextStyleV1 old{
        IRMath::Color{1, 2, 3, 4},
        64,
        TextAlignH::CENTER,
        TextAlignV::BOTTOM,
        120,
        30,
        1,
    };
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));

    ASSERT_EQ(IRWorld::SaveMigration<C_TextStyle>::migrators().size(), 1u);
    const IRAsset::Result<C_TextStyle> restored = migrate<C_TextStyle>(1u, writer.buffer());
    ASSERT_TRUE(restored.ok()) << restored.status_.message_;
    EXPECT_EQ(restored.value_.color_.red_, 1);
    EXPECT_EQ(restored.value_.color_.alpha_, 4);
    EXPECT_EQ(restored.value_.wrapWidth_, 64);
    EXPECT_EQ(restored.value_.alignH_, TextAlignH::CENTER);
    EXPECT_EQ(restored.value_.alignV_, TextAlignV::BOTTOM);
    EXPECT_EQ(restored.value_.boxWidth_, 120);
    EXPECT_EQ(restored.value_.boxHeight_, 30);
    EXPECT_EQ(restored.value_.fontSize_, 1);
    EXPECT_EQ(restored.value_.textSize_, IRRender::TextSize::LARGE);
    EXPECT_EQ(IRWorld::saveVersion<C_TextStyle>(), 2u);
}

TEST(SaveSerializers, TextStyleV1MigrationRejectsAnOutOfRangeAlignment) {
    IRWorld::detail::TextStyleV1
        old{IRMath::IRColors::kWhite, 0, TextAlignH::LEFT, TextAlignV::TOP, 0, 0, 2};
    old.alignH_ = static_cast<TextAlignH>(5);
    IRAsset::MemoryBinaryWriter writer;
    writer.writeBytes(&old, sizeof(old));
    const IRAsset::Result<C_TextStyle> restored = migrate<C_TextStyle>(1u, writer.buffer());
    ASSERT_FALSE(restored.ok());
    EXPECT_EQ(restored.status_.code_, IRAsset::BinaryIOError::UnknownTag);
}

TEST(SaveSerializers, SpriteAnimationRoundTrips) {
    C_SpriteAnimation animation{};
    animation.sheetEntity_ = 4242;
    animation.animationIndex_ = 3;
    animation.frameIndex_ = 5;
    animation.elapsedInFrame_ = 0.125f;
    animation.loopMode_ = SpriteLoopMode::PING_PONG;
    animation.speed_ = 2.5f;
    animation.pingPongDirection_ = -1;
    animation.terminated_ = true;
    animation.stopped_ = true;
    animation.currentAnimName_ = "walk_south";

    const C_SpriteAnimation restored = roundTrip(animation);
    EXPECT_EQ(restored.sheetEntity_, 4242u);
    EXPECT_EQ(restored.animationIndex_, 3);
    EXPECT_EQ(restored.frameIndex_, 5);
    EXPECT_FLOAT_EQ(restored.elapsedInFrame_, 0.125f);
    EXPECT_EQ(restored.loopMode_, SpriteLoopMode::PING_PONG);
    EXPECT_FLOAT_EQ(restored.speed_, 2.5f);
    EXPECT_EQ(restored.pingPongDirection_, -1);
    EXPECT_TRUE(restored.terminated_);
    EXPECT_TRUE(restored.stopped_);
    EXPECT_EQ(restored.currentAnimName_, "walk_south");
    expectConsumesAllBytes(animation);
}

TEST(SaveSerializers, TrianglesOnlySetRoundTrips) {
    C_TrianglesOnlySet set{IRMath::ivec2(2, 3), IRMath::ivec2(-1, 4)};
    set.setTriangle(IRMath::ivec2(0, 0), IRMath::Color{1, 2, 3, 255});
    set.setTriangle(IRMath::ivec2(1, 2), IRMath::Color{9, 8, 7, 255});

    const C_TrianglesOnlySet restored = roundTrip(set);
    EXPECT_EQ(restored.size_.x, 2);
    EXPECT_EQ(restored.size_.y, 3);
    EXPECT_EQ(restored.origin_.x, -1);
    EXPECT_EQ(restored.origin_.y, 4);
    ASSERT_EQ(restored.triangleColors_.size(), set.triangleColors_.size());
    ASSERT_EQ(restored.triangleDistances_.size(), set.triangleDistances_.size());
    EXPECT_EQ(restored.atTriangleColor(IRMath::uvec2(0, 0)).red_, 1);
    EXPECT_EQ(restored.atTriangleColor(IRMath::uvec2(1, 2)).blue_, 7);
    expectConsumesAllBytes(set);
}

TEST(SaveSerializers, TrianglesOnlySetRoundTripsEmpty) {
    const C_TrianglesOnlySet set{};
    const C_TrianglesOnlySet restored = roundTrip(set);
    EXPECT_TRUE(restored.triangleColors_.empty());
    EXPECT_TRUE(restored.triangleDistances_.empty());
    expectConsumesAllBytes(set);
}

// Truncation is already caught by readTrivialVector's short read; this is the
// well-formed-but-inconsistent file — every byte present, but the extent
// disagrees with the two grids it is supposed to describe. Restoring it would
// hand back a component no constructor can build, and the unchecked
// atTriangle* accessors turn that into an out-of-bounds access on first use
// (setTriangle's bounds check is an IR_ASSERT and compiles out under
// IR_RELEASE). TrianglesOnlySetRoundTrips is the positive control for this
// case: the same bytes, unpatched, restore cleanly.
TEST(SaveSerializers, TrianglesOnlySetRejectsExtentGridMismatch) {
    const C_TrianglesOnlySet set{IRMath::ivec2(2, 3), IRMath::ivec2(-1, 4)};
    std::vector<std::uint8_t> bytes = serialize(set);
    // Layout: size_ = i32 x then i32 y, so x occupies bytes [0..3].
    ASSERT_GE(bytes.size(), 4u);
    ASSERT_EQ(bytes[0], 2u) << "expected size_.x at this offset";
    bytes[0] = 3u; // claims a 3x3 = 9-cell extent over the 6 cells on disk

    IRAsset::Result<C_TrianglesOnlySet> restored = deserialize<C_TrianglesOnlySet>(bytes);
    EXPECT_FALSE(restored.ok());
    EXPECT_EQ(restored.status_.code_, IRAsset::BinaryIOError::UnknownTag);
    // The two rejection paths share a status code, so the message is the only
    // thing that tells them apart — pin which one fired.
    EXPECT_NE(restored.status_.message_.find("disagree with the extent"), std::string::npos)
        << "expected the cardinality message, got: " << restored.status_.message_;
}

// A both-negative extent multiplies to a POSITIVE cell count ((-2,-3) -> +6),
// so it agrees with the six colors and six distances actually on disk — a
// product-only consistency test admits it. It must still be rejected: the
// accessors index `index.y * size_.x + index.x`, which goes negative for any
// row past the first. TrianglesOnlySetRoundTrips is the positive control —
// the same bytes, unpatched, restore cleanly.
TEST(SaveSerializers, TrianglesOnlySetRejectsNegativeExtent) {
    const C_TrianglesOnlySet set{IRMath::ivec2(2, 3), IRMath::ivec2(-1, 4)};
    std::vector<std::uint8_t> bytes = serialize(set);
    // Layout: size_ = i32 x then i32 y (little-endian), so x is [0..3], y is [4..7].
    ASSERT_GE(bytes.size(), 8u);
    ASSERT_EQ(bytes[0], 2u) << "expected size_.x at this offset";
    ASSERT_EQ(bytes[4], 3u) << "expected size_.y at this offset";
    bytes[0] = 0xFEu; // size_.x = -2
    bytes[1] = 0xFFu;
    bytes[2] = 0xFFu;
    bytes[3] = 0xFFu;
    bytes[4] = 0xFDu; // size_.y = -3, so size_.x * size_.y == +6 == the grid sizes
    bytes[5] = 0xFFu;
    bytes[6] = 0xFFu;
    bytes[7] = 0xFFu;

    IRAsset::Result<C_TrianglesOnlySet> restored = deserialize<C_TrianglesOnlySet>(bytes);
    EXPECT_FALSE(restored.ok());
    EXPECT_EQ(restored.status_.code_, IRAsset::BinaryIOError::UnknownTag);
    // The sign fault must name itself. The grids DO hold the 6 cells -2 * -3
    // predicts, so the cardinality message would be actively misleading here —
    // that mis-diagnosis is exactly why the two checks are separate.
    EXPECT_NE(restored.status_.message_.find("negative extent"), std::string::npos)
        << "expected the negative-extent message, got: " << restored.status_.message_;
}

// Every field of C_TriangleCanvasBackground is private, so the round trip is
// asserted through byte identity rather than field comparison — see the
// helper's comment.
TEST(SaveSerializers, TriangleCanvasBackgroundRoundTrips) {
    C_TriangleCanvasBackground background{
        BackgroundTypes::kPulsePattern,
        std::vector<IRMath::Color>{IRMath::Color{10, 20, 30, 255}, IRMath::Color{40, 50, 60, 255}},
        IRMath::ivec2(4, 4),
        2.5f,
        7
    };
    background.setPulseWaveDirection(0.0f, 1.0f, 3.0f);
    background.setPulseWaveInterference(1.0f, 0.0f, 5.0f, 0.25f);
    background.setPulseWavePrimaryTiming(1.5f, 0.75f);
    background.setPulseWaveDirectionLinearMotion(
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        4.0f,
        IREasingFunctions::kCubicEaseIn,
        IREasingFunctions::kCubicEaseOut
    );
    background.setPatternZoomMultiplier(4.0f);

    expectReserializesIdentically(background);
    expectConsumesAllBytes(background);
}

TEST(SaveSerializers, TriangleCanvasBackgroundRoundTripsDefault) {
    const C_TriangleCanvasBackground background{};
    expectReserializesIdentically(background);
    expectConsumesAllBytes(background);
}

TEST(SaveSerializers, WidgetPanelRoundTrips) {
    C_WidgetPanel panel{};
    panel.title_ = "Inspector";
    panel.drawBorder_ = false;
    const C_WidgetPanel restored = roundTrip(panel);
    EXPECT_EQ(restored.title_, "Inspector");
    EXPECT_FALSE(restored.drawBorder_);
    expectConsumesAllBytes(panel);
}

TEST(SaveSerializers, WidgetLabelRoundTrips) {
    C_WidgetLabel label{};
    label.text_ = "Frames";
    label.colorOverride_ = IRMath::Color{12, 34, 56, 200};
    label.textSize_ = IRRender::TextSize::SMALL;
    const C_WidgetLabel restored = roundTrip(label);
    EXPECT_EQ(restored.text_, "Frames");
    EXPECT_EQ(restored.colorOverride_.red_, 12);
    EXPECT_EQ(restored.colorOverride_.green_, 34);
    EXPECT_EQ(restored.colorOverride_.blue_, 56);
    EXPECT_EQ(restored.colorOverride_.alpha_, 200);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    EXPECT_EQ(roundTrip(C_WidgetLabel{}).textSize_, IRRender::TextSize::LARGE);
    expectConsumesAllBytes(label);
    expectTruncationIsRecoverable(label);
    expectOutOfRangeTextSizeIsRejected(label, 2);
}

TEST(SaveSerializers, WidgetButtonRoundTrips) {
    C_WidgetButton button{};
    button.label_ = "Save";
    button.textSize_ = IRRender::TextSize::SMALL;
    const C_WidgetButton restored = roundTrip(button);
    EXPECT_EQ(restored.label_, "Save");
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    expectConsumesAllBytes(button);
    expectOutOfRangeTextSizeIsRejected(button, 2);
}

TEST(SaveSerializers, WidgetSliderRoundTrips) {
    C_WidgetSlider slider{};
    slider.label_ = "Zoom";
    slider.minValue_ = -2.0f;
    slider.maxValue_ = 8.0f;
    slider.currentValue_ = 3.25f;
    slider.textSize_ = IRRender::TextSize::SMALL;
    const C_WidgetSlider restored = roundTrip(slider);
    EXPECT_EQ(restored.label_, "Zoom");
    EXPECT_FLOAT_EQ(restored.minValue_, -2.0f);
    EXPECT_FLOAT_EQ(restored.maxValue_, 8.0f);
    EXPECT_FLOAT_EQ(restored.currentValue_, 3.25f);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    expectConsumesAllBytes(slider);
    expectOutOfRangeTextSizeIsRejected(slider, 2);
}

TEST(SaveSerializers, WidgetCheckboxRoundTrips) {
    C_WidgetCheckbox checkbox{};
    checkbox.label_ = "Wireframe";
    checkbox.checked_ = true;
    checkbox.textSize_ = IRRender::TextSize::SMALL;
    const C_WidgetCheckbox restored = roundTrip(checkbox);
    EXPECT_EQ(restored.label_, "Wireframe");
    EXPECT_TRUE(restored.checked_);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    expectConsumesAllBytes(checkbox);
    expectOutOfRangeTextSizeIsRejected(checkbox, 2);
}

TEST(SaveSerializers, WidgetListRoundTrips) {
    C_WidgetList list{};
    list.items_ = {"alpha", "", "gamma"}; // includes an empty-string element
    list.selectedIndex_ = 2;
    list.scrollOffset_ = 1;
    list.itemHeight_ = 24;
    const C_WidgetList restored = roundTrip(list);
    ASSERT_EQ(restored.items_.size(), 3u);
    EXPECT_EQ(restored.items_[0], "alpha");
    EXPECT_EQ(restored.items_[1], "");
    EXPECT_EQ(restored.items_[2], "gamma");
    EXPECT_EQ(restored.selectedIndex_, 2);
    EXPECT_EQ(restored.scrollOffset_, 1);
    EXPECT_EQ(restored.itemHeight_, 24);
    expectConsumesAllBytes(list);
}

TEST(SaveSerializers, WidgetListRoundTripsEmptyItems) {
    const C_WidgetList list{};
    EXPECT_TRUE(roundTrip(list).items_.empty());
    expectConsumesAllBytes(list);
}

TEST(SaveSerializers, WidgetDropdownRoundTrips) {
    C_WidgetDropdown dropdown{};
    dropdown.items_ = {"one", "two"};
    dropdown.selectedIndex_ = 1;
    dropdown.isOpen_ = true;
    dropdown.itemHeight_ = 20;
    dropdown.textSize_ = IRRender::TextSize::SMALL;
    const C_WidgetDropdown restored = roundTrip(dropdown);
    ASSERT_EQ(restored.items_.size(), 2u);
    EXPECT_EQ(restored.items_[1], "two");
    EXPECT_EQ(restored.selectedIndex_, 1);
    EXPECT_TRUE(restored.isOpen_);
    EXPECT_EQ(restored.itemHeight_, 20);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    expectConsumesAllBytes(dropdown);
    expectOutOfRangeTextSizeIsRejected(dropdown, 2);
}

// Each version 1 record below is spelled out field by field in the layout
// that version wrote, so a reader that drifted from it fails here.
TEST(SaveSerializers, TextBearingWidgetV1MigrationsPreserveEveryFieldAndReadLarge) {
    {
        IRAsset::MemoryBinaryWriter v1;
        v1.writeString("Frames");
        v1.writeU32(IRMath::Color{12, 34, 56, 200}.toPackedRGBA());
        const IRAsset::Result<C_WidgetLabel> label = migrate<C_WidgetLabel>(1u, v1.buffer());
        ASSERT_TRUE(label.ok()) << label.status_.message_;
        EXPECT_EQ(label.value_.text_, "Frames");
        EXPECT_EQ(label.value_.colorOverride_.blue_, 56);
        EXPECT_EQ(label.value_.colorOverride_.alpha_, 200);
        EXPECT_EQ(label.value_.textSize_, IRRender::TextSize::LARGE);
    }
    {
        IRAsset::MemoryBinaryWriter v1;
        v1.writeString("Save");
        const IRAsset::Result<C_WidgetButton> button = migrate<C_WidgetButton>(1u, v1.buffer());
        ASSERT_TRUE(button.ok()) << button.status_.message_;
        EXPECT_EQ(button.value_.label_, "Save");
        EXPECT_EQ(button.value_.textSize_, IRRender::TextSize::LARGE);
    }
    {
        IRAsset::MemoryBinaryWriter v1;
        v1.writeString("Zoom");
        v1.writeF32(-2.0f);
        v1.writeF32(8.0f);
        v1.writeF32(3.25f);
        const IRAsset::Result<C_WidgetSlider> slider = migrate<C_WidgetSlider>(1u, v1.buffer());
        ASSERT_TRUE(slider.ok()) << slider.status_.message_;
        EXPECT_EQ(slider.value_.label_, "Zoom");
        EXPECT_FLOAT_EQ(slider.value_.minValue_, -2.0f);
        EXPECT_FLOAT_EQ(slider.value_.maxValue_, 8.0f);
        EXPECT_FLOAT_EQ(slider.value_.currentValue_, 3.25f);
        EXPECT_EQ(slider.value_.textSize_, IRRender::TextSize::LARGE);
    }
    {
        IRAsset::MemoryBinaryWriter v1;
        v1.writeString("Wireframe");
        v1.writeU8(1);
        const IRAsset::Result<C_WidgetCheckbox> checkbox =
            migrate<C_WidgetCheckbox>(1u, v1.buffer());
        ASSERT_TRUE(checkbox.ok()) << checkbox.status_.message_;
        EXPECT_EQ(checkbox.value_.label_, "Wireframe");
        EXPECT_TRUE(checkbox.value_.checked_);
        EXPECT_EQ(checkbox.value_.textSize_, IRRender::TextSize::LARGE);
    }
    {
        IRAsset::MemoryBinaryWriter v1;
        IRWorld::detail::writeStringVector(v1, {"one", "two"});
        v1.writeI32(1);
        v1.writeU8(1);
        v1.writeI32(20);
        const IRAsset::Result<C_WidgetDropdown> dropdown =
            migrate<C_WidgetDropdown>(1u, v1.buffer());
        ASSERT_TRUE(dropdown.ok()) << dropdown.status_.message_;
        ASSERT_EQ(dropdown.value_.items_.size(), 2u);
        EXPECT_EQ(dropdown.value_.items_[1], "two");
        EXPECT_EQ(dropdown.value_.selectedIndex_, 1);
        EXPECT_TRUE(dropdown.value_.isOpen_);
        EXPECT_EQ(dropdown.value_.itemHeight_, 20);
        EXPECT_EQ(dropdown.value_.textSize_, IRRender::TextSize::LARGE);
    }
    EXPECT_EQ(IRWorld::saveVersion<C_WidgetLabel>(), 2u);
    EXPECT_EQ(IRWorld::saveVersion<C_WidgetButton>(), 2u);
    EXPECT_EQ(IRWorld::saveVersion<C_WidgetSlider>(), 2u);
    EXPECT_EQ(IRWorld::saveVersion<C_WidgetCheckbox>(), 2u);
    EXPECT_EQ(IRWorld::saveVersion<C_WidgetDropdown>(), 2u);
}

TEST(SaveSerializers, WidgetRadioRoundTrips) {
    C_WidgetRadio radio{};
    radio.label_ = "Metal";
    radio.groupId_ = 77;
    radio.value_ = -3;
    radio.selected_ = true;
    const C_WidgetRadio restored = roundTrip(radio);
    EXPECT_EQ(restored.label_, "Metal");
    EXPECT_EQ(restored.groupId_, 77u);
    EXPECT_EQ(restored.value_, -3);
    EXPECT_TRUE(restored.selected_);
    expectConsumesAllBytes(radio);
}

TEST(SaveSerializers, WidgetTextInputRoundTrips) {
    C_WidgetTextInput input{};
    input.text_ = "entity name";
    input.cursorPos_ = 6;
    input.maxLength_ = 64;
    const C_WidgetTextInput restored = roundTrip(input);
    EXPECT_EQ(restored.text_, "entity name");
    EXPECT_EQ(restored.cursorPos_, 6);
    EXPECT_EQ(restored.maxLength_, 64);
    expectConsumesAllBytes(input);
}

TEST(SaveSerializers, WidgetNumberFieldRoundTripsAndDropsEditState) {
    C_WidgetNumberField field{};
    field.value_ = 3.5f;
    field.minValue_ = -2.0f;
    field.maxValue_ = 8.0f;
    field.step_ = 0.5f;
    field.textSize_ = IRRender::TextSize::SMALL;
    field.editBuffer_ = "7.5";
    field.editing_ = true;
    const C_WidgetNumberField restored = roundTrip(field);
    EXPECT_FLOAT_EQ(restored.value_, 3.5f);
    EXPECT_FLOAT_EQ(restored.minValue_, -2.0f);
    EXPECT_FLOAT_EQ(restored.maxValue_, 8.0f);
    EXPECT_FLOAT_EQ(restored.step_, 0.5f);
    EXPECT_EQ(restored.textSize_, IRRender::TextSize::SMALL);
    EXPECT_FALSE(restored.editing_);
    EXPECT_TRUE(restored.editBuffer_.empty());
    expectConsumesAllBytes(field);
    expectTruncationIsRecoverable(field);
    expectOutOfRangeTextSizeIsRejected(field, 2);
}

TEST(SaveSerializers, WidgetNumberFieldRejectsInvalidStoredValues) {
    const auto invalid = [](float value, float minValue, float maxValue, float step) {
        IRAsset::MemoryBinaryWriter writer;
        writer.writeF32(value);
        writer.writeF32(minValue);
        writer.writeF32(maxValue);
        writer.writeF32(step);
        writer.writeU8(static_cast<std::uint8_t>(IRRender::TextSize::LARGE));
        return deserialize<C_WidgetNumberField>(writer.buffer());
    };
    EXPECT_FALSE(invalid(0.0f, 1.0f, 1.0f, 0.0f).ok());
    EXPECT_FALSE(invalid(0.0f, std::numeric_limits<float>::infinity(), 1.0f, 0.0f).ok());
    EXPECT_FALSE(invalid(0.0f, 0.0f, std::numeric_limits<float>::infinity(), 0.0f).ok());
    EXPECT_FALSE(invalid(0.0f, 0.0f, 1.0f, -1.0f).ok());
    EXPECT_FALSE(invalid(2.0f, 0.0f, 1.0f, 0.0f).ok());
    EXPECT_FALSE(invalid(std::numeric_limits<float>::infinity(), 0.0f, 1.0f, 0.0f).ok());
    EXPECT_FALSE(invalid(0.0f, 0.0f, 1.0f, std::numeric_limits<float>::infinity()).ok());
}

// --- truncation ------------------------------------------------------------

TEST(SaveSerializers, TruncatedPayloadsFailCleanly) {
    expectTruncationIsRecoverable(C_Name{"abc"});

    C_WidgetList list{};
    list.items_ = {"aa", "bb"};
    expectTruncationIsRecoverable(list);

    C_Skeleton skeleton{};
    skeleton.joints_ = {1, 2};
    expectTruncationIsRecoverable(skeleton);

    C_BindPoints points{};
    points.setPoint("k", BindPointRuntime{1, IRMath::vec3(0.0f), IRMath::vec4(0.0f)});
    expectTruncationIsRecoverable(points);
}

// --- derived registry membership ------------------------------------------

// makeDefaultSaveRegistry resolves a session-local ComponentId per entry, so
// these tests need a live EntityManager where the pure-serializer tests above
// do not.
class DefaultRegistryTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

// The process-default registry's membership is *derived* from
// save_component_inventory.hpp, not a curated list. If someone re-adds a
// hand-written registerComponent line, or an
// opt-in silently stops being registered, these two counts diverge.
TEST_F(DefaultRegistryTest, MembershipIsDerivedFromInventory) {
    const IRWorld::SaveRegistry registry = IRWorld::makeDefaultSaveRegistry();
    constexpr std::size_t expected = IRWorld::detail::countOptIns<IRWorld::AllEngineComponents>();

    EXPECT_EQ(registry.size(), expected);
    // Positive-fire guard: a four-entry curated registry would not prove
    // inventory-derived membership.
    EXPECT_GT(registry.size(), 4u);
}

// Heap-owning components must be reachable through the process-default
// registry by their on-disk name; the Lua IRPersist surface uses this registry.
TEST_F(DefaultRegistryTest, ResolvesHeapOwningComponents) {
    const IRWorld::SaveRegistry registry = IRWorld::makeDefaultSaveRegistry();
    for (const char *name :
         {"IRComponents::C_Name",
          "IRComponents::C_Skeleton",
          "IRComponents::C_MidiSequence",
          "IRComponents::C_TextSegment",
          "IRComponents::C_WidgetLabel",
          "IRComponents::C_BindPoints"}) {
        EXPECT_NE(registry.findByName(name), nullptr) << name << " is not registered";
    }
}

// The registry is what the Lua IRPersist surface resolves against, so a name
// that never lands there is a component that silently does not persist.
TEST_F(DefaultRegistryTest, ResolvesEnumStoredEasingComponents) {
    const IRWorld::SaveRegistry registry = IRWorld::makeDefaultSaveRegistry();
    for (const char *name : {"IRComponents::C_GotoEasing3D", "IRComponents::C_RotationTarget"}) {
        EXPECT_NE(registry.findByName(name), nullptr) << name << " is not registered";
    }
}

// The engine's own camera is persistent and never enters a snapshot, so the
// policy column is exercised on a plain carrier entity: the authored flag
// survives a save and load, and the transient phase beside it does not.
TEST_F(DefaultRegistryTest, CameraZoomPolicySurvivesALoadAndThePhaseDoesNot) {
    const IRWorld::SaveRegistry registry = IRWorld::makeDefaultSaveRegistry();
    C_Camera camera;
    camera.continuousZoom_ = true;
    C_CameraZoomFrameState state;
    state.published_ = true;
    state.sample_.phase_ = IRMath::dvec2(0.25, 0.75);
    const IREntity::EntityId carrier = IREntity::createEntity(camera, state);

    const std::string path = testing::TempDir() + "/ir_camera_zoom_policy.irws";
    ASSERT_TRUE(IRWorld::saveWorld(registry, path).ok());
    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);

    const IRWorld::LoadResult result = IRWorld::loadWorld(registry, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    ASSERT_TRUE(m_entity_manager.entityExists(carrier));
    EXPECT_TRUE(IREntity::getComponent<C_Camera>(carrier).continuousZoom_);
    EXPECT_FALSE(IREntity::getComponentOptional<C_CameraZoomFrameState>(carrier).has_value());
}

// C_LerpEntity holds an arbitrary std::function with no authored identity to
// recover, so no honest serializer exists for it. Pinned here so a future
// re-opt-in has to confront the callback problem rather than silently shipping
// a lossy default. Its enum-storing siblings are covered by the opt-in case
// below.
TEST(SaveSerializers, CallbackBearingComponentsStayOptedOut) {
    EXPECT_FALSE(IRWorld::shouldSave<C_LerpEntity>());
}

TEST(SaveSerializers, EnumStoredEasingComponentsOptIn) {
    EXPECT_TRUE(IRWorld::shouldSave<C_GotoEasing3D>());
    EXPECT_TRUE(IRWorld::shouldSave<C_RotationTarget>());
    EXPECT_EQ(IRWorld::saveVersion<C_GotoEasing3D>(), 1u);
    EXPECT_EQ(IRWorld::saveVersion<C_RotationTarget>(), 1u);
}

// The raw-image arm is what makes the two components above serializable
// without a specialization. Hand-written layouts declare that choice through
// the inventory instead.
TEST(SaveSerializers, EnumStoredEasingComponentsAreTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<C_GotoEasing3D>);
    EXPECT_TRUE(std::is_trivially_copyable_v<C_RotationTarget>);
}

} // namespace
