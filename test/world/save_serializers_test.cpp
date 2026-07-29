#include <gtest/gtest.h>

#include <irreden/world/save_component_inventory.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/audio/save_serializers_audio.hpp>
#include <irreden/common/save_serializers_common.hpp>
#include <irreden/demo/save_serializers_demo.hpp>
#include <irreden/render/save_serializers_render.hpp>
#include <irreden/update/save_serializers_update.hpp>
#include <irreden/voxel/save_serializers_voxel.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_entity.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Per-component SaveSerialize<C> coverage for the heap-owning components the
// process-default registry gained in #2242, plus the membership assertion
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

// --- voxel/ ----------------------------------------------------------------

TEST(SaveSerializers, JointNameRoundTrips) {
    C_JointName joint{};
    joint.name_ = "spine.02";
    EXPECT_EQ(roundTrip(joint).name_, "spine.02");
    expectConsumesAllBytes(joint);
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

// --- render/ ---------------------------------------------------------------

TEST(SaveSerializers, TextSegmentRoundTrips) {
    EXPECT_EQ(roundTrip(C_TextSegment{"score: 42"}).text_, "score: 42");
    expectConsumesAllBytes(C_TextSegment{""});
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
    const C_WidgetLabel restored = roundTrip(label);
    EXPECT_EQ(restored.text_, "Frames");
    EXPECT_EQ(restored.colorOverride_.red_, 12);
    EXPECT_EQ(restored.colorOverride_.green_, 34);
    EXPECT_EQ(restored.colorOverride_.blue_, 56);
    EXPECT_EQ(restored.colorOverride_.alpha_, 200);
    expectConsumesAllBytes(label);
}

TEST(SaveSerializers, WidgetButtonRoundTrips) {
    C_WidgetButton button{};
    button.label_ = "Save";
    EXPECT_EQ(roundTrip(button).label_, "Save");
    expectConsumesAllBytes(button);
}

TEST(SaveSerializers, WidgetSliderRoundTrips) {
    C_WidgetSlider slider{};
    slider.label_ = "Zoom";
    slider.minValue_ = -2.0f;
    slider.maxValue_ = 8.0f;
    slider.currentValue_ = 3.25f;
    const C_WidgetSlider restored = roundTrip(slider);
    EXPECT_EQ(restored.label_, "Zoom");
    EXPECT_FLOAT_EQ(restored.minValue_, -2.0f);
    EXPECT_FLOAT_EQ(restored.maxValue_, 8.0f);
    EXPECT_FLOAT_EQ(restored.currentValue_, 3.25f);
    expectConsumesAllBytes(slider);
}

TEST(SaveSerializers, WidgetCheckboxRoundTrips) {
    C_WidgetCheckbox checkbox{};
    checkbox.label_ = "Wireframe";
    checkbox.checked_ = true;
    const C_WidgetCheckbox restored = roundTrip(checkbox);
    EXPECT_EQ(restored.label_, "Wireframe");
    EXPECT_TRUE(restored.checked_);
    expectConsumesAllBytes(checkbox);
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
    const C_WidgetDropdown restored = roundTrip(dropdown);
    ASSERT_EQ(restored.items_.size(), 2u);
    EXPECT_EQ(restored.items_[1], "two");
    EXPECT_EQ(restored.selectedIndex_, 1);
    EXPECT_TRUE(restored.isOpen_);
    EXPECT_EQ(restored.itemHeight_, 20);
    expectConsumesAllBytes(dropdown);
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

// --- truncation ------------------------------------------------------------

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

// The headline acceptance criterion for #2242: the process-default registry's
// membership is *derived* from save_component_inventory.hpp, not a curated
// list. If someone re-adds a hand-written registerComponent line, or an
// opt-in silently stops being registered, these two counts diverge.
TEST_F(DefaultRegistryTest, MembershipIsDerivedFromInventory) {
    const IRWorld::SaveRegistry registry = IRWorld::makeDefaultSaveRegistry();
    constexpr std::size_t expected = IRWorld::detail::countOptIns<IRWorld::AllEngineComponents>();

    EXPECT_EQ(registry.size(), expected);
    // Positive-fire guard: the pre-#2242 curated registry held 4 entries, so
    // an assertion that passed at 4 would prove nothing about "derived".
    EXPECT_GT(registry.size(), 4u);
}

// Spot-check that components which previously had no serializer are now
// actually reachable through the process-default registry by their on-disk
// name — the registry is what the Lua IRPersist surface uses.
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

// The two components #2242 flipped OPT_IN -> OPT_OUT: both store a resolved
// std::function easing curve, so no honest serializer exists for them. Pinned
// here so a future re-opt-in has to confront the callback problem rather than
// silently shipping a lossy default.
TEST(SaveSerializers, CallbackBearingComponentsStayOptedOut) {
    EXPECT_FALSE(IRWorld::shouldSave<C_GotoEasing3D>());
    EXPECT_FALSE(IRWorld::shouldSave<C_RotationTarget>());
    EXPECT_FALSE(IRWorld::shouldSave<C_LerpEntity>());
}

} // namespace
