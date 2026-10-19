#ifndef GLITCH_SUBJECT_FIXTURE_H
#define GLITCH_SUBJECT_FIXTURE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/glitch_subject.hpp>
#include <irreden/render/trixel_glitch_math.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// `--glitch-subject-test <mode>`: the canvas-space subject glitch's evidence
// fixture (TRIXEL_GLITCH).
//
// `standard-off` registers the pass with no subject under the standing shot
// table, so its captures must equal the default references. Every other mode
// adds two striped voxel subjects in empty sky at a fixed seed and phase and
// captures `glitch_subject_*` shots. On each capture frame the probe reads
// the main canvas back (glitched colour, the pre-glitch input left in the
// scratch plane, ids, distances, history), replays the kernel through the CPU
// twin in trixel_glitch_math.hpp and logs the mismatch count plus the effect
// statistics each mode is graded on. The `spill-*` modes additionally pose
// the subjects far and near on two earlier frames of the same period tick and
// log the interference oracle.
namespace IRShapeDebug {

struct GlitchSubjectFixture {
    enum class Mode {
        NONE,
        STANDARD_OFF,
        OFF,
        TEAR,
        HOLD,
        SPLIT,
        ECHO,
        DISSOLVE,
        DEPTH,
        SPILL_FAR_OFF,
        SPILL_FAR_ON,
        SPILL_NEAR_OFF,
        SPILL_NEAR_ON,
        CHOREOGRAPHY,
        ALL,
    };

    struct ModeInfo {
        Mode mode_;
        const char *arg_;
        const char *label_;
    };

    static constexpr std::array<ModeInfo, 15> kModes{{
        {Mode::NONE, "none", ""},
        {Mode::STANDARD_OFF, "standard-off", ""},
        {Mode::OFF, "off", "glitch_subject_off"},
        {Mode::TEAR, "tear", "glitch_subject_tear"},
        {Mode::HOLD, "hold", "glitch_subject_hold"},
        {Mode::SPLIT, "split", "glitch_subject_split"},
        {Mode::ECHO, "echo", "glitch_subject_echo"},
        {Mode::DISSOLVE, "dissolve", "glitch_subject_dissolve"},
        {Mode::DEPTH, "depth", "glitch_subject_depth"},
        {Mode::SPILL_FAR_OFF, "spill-far-off", "glitch_subject_spill_far_off"},
        {Mode::SPILL_FAR_ON, "spill-far-on", "glitch_subject_spill_far_on"},
        {Mode::SPILL_NEAR_OFF, "spill-near-off", "glitch_subject_spill_near_off"},
        {Mode::SPILL_NEAR_ON, "spill-near-on", "glitch_subject_spill_near_on"},
        {Mode::CHOREOGRAPHY, "choreography", "glitch_subject_choreo_tick_n"},
        {Mode::ALL, "all", "glitch_subject_all"},
    }};
    static constexpr const char *kChoreographySecondLabel = "glitch_subject_choreo_tick_n1";

    // Period ticks start at engine tick 42, the first frame drawn at the shot's
    // zoom, and last 60 ticks, so the capture (warmup 40 + settle) lands in
    // period tick 0 and the hold history is the tick-42 pose. The
    // choreography period is five engine ticks with the same phase and its
    // two shots keep the default settle, so its captures (ticks 44 and 49)
    // land on period ticks 0 and 1.
    static constexpr double kPeriodSeconds = 1.0;
    static constexpr double kPhaseSeconds = 42.0 / 60.0;
    static constexpr double kChoreographyPeriodSeconds = 5.0 / 60.0;
    static constexpr std::uint32_t kSeed = 11u;

    // Settle frames of a single-shot capture: the shot's zoom lands at engine
    // tick 41, and the depth measurement and spill sequence below must run at
    // that zoom before the capture at tick 55, still in period tick 0.
    static constexpr int kSettleFrames = 14;
    // Engine ticks the spill modes pose the subjects far and near on; each
    // pose holds three ticks and is read on its last, after the RENDER-phase
    // write has propagated.
    static constexpr std::uint64_t kSpillFarStart = 43;
    static constexpr std::uint64_t kSpillNearStart = 46;
    static constexpr std::uint64_t kSpillPoseTicks = 3;
    // Engine tick the depth mode reads subject A's depth range on.
    static constexpr std::uint64_t kDepthMeasureTick = 44;
    // First engine tick the pre-glitch checksums are taken on.
    static constexpr std::uint64_t kPreHashFirstTick = 40;

    static constexpr IRMath::vec3 kSubjectA{-26.0f, 6.0f, 10.0f};
    static constexpr IRMath::vec3 kSubjectB{-34.0f, 26.0f, 16.0f};
    // A world move along (1, -1, 0) is a pure horizontal canvas shift at
    // unchanged depth, which keeps every row-hashed effect comparable.
    static constexpr IRMath::vec3 kSpillNearShift{8.0f, -8.0f, 0.0f};
    static constexpr IRMath::ivec3 kSubjectSizeA{6, 6, 6};
    static constexpr IRMath::ivec3 kSubjectSizeB{5, 5, 5};

    Mode mode_ = Mode::NONE;
    std::array<IREntity::EntityId, 8> subjects_{};
    int subjectCount_ = 0;
    bool depthMeasured_ = false;
    int depthNear_ = 0;
    int depthFar_ = 0;

    // Pre-glitch id and distance checksums of the current frame.
    std::uint64_t preIdHash_ = 0;
    std::uint64_t preDistanceHash_ = 0;
    std::uint64_t preHashTick_ = ~0ull;

    struct SpillPose {
        bool valid_ = false;
        IRMath::ivec2 size_{0};
        std::vector<int> slots_;
        std::vector<std::uint8_t> effect_;
    };
    SpillPose far_;
    SpillPose near_;

    static const ModeInfo &info(std::string_view arg) {
        for (const ModeInfo &modeInfo : kModes) {
            if (arg == modeInfo.arg_) {
                return modeInfo;
            }
        }
        return kModes[0];
    }

    bool captures() const {
        return mode_ != Mode::NONE && mode_ != Mode::STANDARD_OFF;
    }

    bool isSpill() const {
        return mode_ == Mode::SPILL_FAR_OFF || mode_ == Mode::SPILL_FAR_ON ||
               mode_ == Mode::SPILL_NEAR_OFF || mode_ == Mode::SPILL_NEAR_ON;
    }

    // One shot per mode, two for the choreography.
    const IRVideo::AutoScreenshotShot *shots(int &count) const {
        static const std::array<IRVideo::AutoScreenshotShot, kModes.size() + 1> table = [] {
            std::array<IRVideo::AutoScreenshotShot, kModes.size() + 1> rows{};
            for (std::size_t i = 0; i < kModes.size(); ++i) {
                rows[i] = {4.0f, IRMath::vec2(0.0f), 0.0f, kModes[i].label_};
            }
            rows[kModes.size()] = {4.0f, IRMath::vec2(0.0f), 0.0f, kChoreographySecondLabel};
            return rows;
        }();
        for (std::size_t i = 0; i < kModes.size(); ++i) {
            if (kModes[i].mode_ == mode_) {
                count = mode_ == Mode::CHOREOGRAPHY ? 2 : 1;
                return &table[i];
            }
        }
        count = 0;
        return nullptr;
    }

    static IRComponents::C_GlitchState clockState(Mode mode) {
        IRComponents::C_GlitchState state;
        state.periodSeconds_ =
            mode == Mode::CHOREOGRAPHY ? kChoreographyPeriodSeconds : kPeriodSeconds;
        state.phaseSeconds_ = kPhaseSeconds;
        state.seed_ = kSeed;
        return state;
    }

    // Two-tone stripes so a whole-trixel shift reads inside a face, not only
    // at its silhouette.
    static IREntity::EntityId spawnSubject(
        IRMath::vec3 position, IRMath::ivec3 size, IRMath::Color base, IRMath::Color stripe
    ) {
        IRComponents::C_VoxelSetNew set{size, base, true};
        set.editVoxels([&](int, IRComponents::C_Voxel &voxel, IRMath::vec3 local) {
            const int band = static_cast<int>(IRMath::floor(local.x + local.z));
            if ((band & 1) != 0) {
                voxel.color_ = stripe;
            }
        });
        return IREntity::createEntity(IRComponents::C_LocalTransform{position}, set);
    }

    void spawn() {
        subjects_[0] = spawnSubject(
            kSubjectA,
            kSubjectSizeA,
            IRMath::Color{236, 126, 54, 255},
            IRMath::Color{248, 214, 92, 255}
        );
        subjects_[1] = spawnSubject(
            kSubjectB,
            kSubjectSizeB,
            IRMath::Color{58, 150, 232, 255},
            IRMath::Color{168, 96, 236, 255}
        );
        subjectCount_ = 2;
        if (mode_ != Mode::ALL) {
            return;
        }
        // Six more in two rows below A, spaced so neighbours' spill bands
        // overlap and interfere.
        for (int i = 0; i < 6; ++i) {
            const float column = static_cast<float>(i % 3);
            const float row = static_cast<float>(i / 3);
            subjects_[static_cast<std::size_t>(2 + i)] = spawnSubject(
                kSubjectA + IRMath::vec3(-5.0f * column, 5.0f * column, 9.0f + 9.0f * row),
                IRMath::ivec3(4),
                IRMath::Color{static_cast<std::uint8_t>(80 + 25 * i), 200, 120, 255},
                IRMath::Color{40, static_cast<std::uint8_t>(90 + 20 * i), 210, 255}
            );
        }
        subjectCount_ = 8;
    }

    static IRComponents::C_GlitchSubject inactive() {
        IRComponents::C_GlitchSubject subject;
        subject.weight_ = 0.0f;
        return subject;
    }

    // The subject configuration of @p mode, with @p enabled false for the
    // controls (the subjects stay marked but inactive, so nothing dispatches).
    IRComponents::C_GlitchSubject subjectFor(int index, bool enabled) const {
        IRComponents::C_GlitchSubject subject;
        if (!enabled) {
            return inactive();
        }
        switch (mode_) {
        case Mode::TEAR:
            subject.tearWeight_ = 1.0f;
            subject.spill_ = 2;
            break;
        case Mode::HOLD:
            // Some bands freeze and some stay live, so the seam shows.
            subject.holdWeight_ = 0.6f;
            break;
        case Mode::SPLIT:
            subject.splitWeight_ = 1.0f;
            subject.spill_ = 2;
            break;
        case Mode::ECHO:
            subject.echoWeight_ = 1.0f;
            subject.spill_ = 3;
            break;
        case Mode::DISSOLVE:
            subject.dissolveWeight_ = 0.6f;
            break;
        case Mode::DEPTH:
            if (index != 0 || !depthMeasured_) {
                return inactive();
            }
            // Half the bands hold, so the tear shows in the rest.
            subject.tearWeight_ = 1.0f;
            subject.holdWeight_ = 0.5f;
            subject.depthEnabled_ = true;
            subject.depthNear_ = depthNear_;
            subject.depthFar_ = depthFar_;
            break;
        case Mode::SPILL_FAR_ON:
        case Mode::SPILL_NEAR_ON:
        case Mode::SPILL_FAR_OFF:
        case Mode::SPILL_NEAR_OFF:
            subject.tearWeight_ = 1.0f;
            subject.splitWeight_ = 1.0f;
            subject.echoWeight_ = 1.0f;
            subject.spill_ = IRPrefab::Glitch::kSubjectMaxSpill;
            break;
        case Mode::CHOREOGRAPHY:
            if (index != 0) {
                return inactive();
            }
            subject.tearWeight_ = 1.0f;
            subject.echoWeight_ = 1.0f;
            subject.spill_ = 3;
            break;
        case Mode::ALL:
            subject.tearWeight_ = 1.0f;
            subject.holdWeight_ = 1.0f;
            subject.splitWeight_ = 1.0f;
            subject.echoWeight_ = 1.0f;
            subject.dissolveWeight_ = 1.0f;
            subject.spill_ = IRPrefab::Glitch::kSubjectMaxSpill;
            break;
        default:
            return inactive();
        }
        return subject;
    }

    // Spill pose of this tick: the sequenced far/near reads, else the mode's
    // own pose.
    bool spillNearThisTick(std::uint64_t tick, bool &enabled) const {
        if (tick >= kSpillFarStart && tick < kSpillFarStart + kSpillPoseTicks) {
            enabled = true;
            return false;
        }
        if (tick >= kSpillNearStart && tick < kSpillNearStart + kSpillPoseTicks) {
            enabled = true;
            return true;
        }
        enabled = mode_ == Mode::SPILL_FAR_ON || mode_ == Mode::SPILL_NEAR_ON;
        return mode_ == Mode::SPILL_NEAR_OFF || mode_ == Mode::SPILL_NEAR_ON;
    }

    static std::uint64_t fnv(std::uint64_t hash, std::uint32_t value) {
        return (hash ^ value) * 0x100000001B3ull;
    }

    static const IRComponents::C_TriangleCanvasTextures &mainCanvas() {
        return IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(
            IRRender::getCanvas("main")
        );
    }

    // RENDER, before VOXEL_TO_TRIXEL_STAGE_1: poses and configures the
    // subjects for this tick, and drives the choreography's two passes.
    void controlFrame() {
        const std::uint64_t tick = IRTime::tick();
        if (mode_ == Mode::DEPTH && !depthMeasured_ && tick >= kDepthMeasureTick) {
            measureDepthRange();
        }
        if (mode_ == Mode::HOLD || mode_ == Mode::DEPTH) {
            // Animated within the period tick, so the tick-start history differs
            // from the live pose. Along (1, -1, 0), which keeps every depth.
            const float step = static_cast<float>(tick % 4u);
            IREntity::getComponent<IRComponents::C_LocalTransform>(subjects_[0]).translation_ =
                kSubjectA + IRMath::vec3(step, -step, 0.0f);
        }
        bool enabled = mode_ != Mode::OFF;
        if (isSpill()) {
            const bool near = spillNearThisTick(tick, enabled);
            IREntity::getComponent<IRComponents::C_LocalTransform>(subjects_[1]).translation_ =
                kSubjectB + (near ? kSpillNearShift : IRMath::vec3(0.0f));
        }
        if (mode_ == Mode::CHOREOGRAPHY) {
            const std::int64_t periodTick = IRPrefab::Glitch::clock().tick_;
            const bool subjectTick = (periodTick & 1) == 0;
            enabled = subjectTick;
            IRComponents::C_GlitchState state = clockState(mode_);
            if (!subjectTick) {
                state.intensity_ = 1.0f;
                state.tearWeight_ = 1.0f;
            }
            IRPrefab::Glitch::setState(state);
        }
        for (int i = 0; i < subjectCount_; ++i) {
            IRPrefab::Glitch::setSubject(
                subjects_[static_cast<std::size_t>(i)],
                subjectFor(i, enabled)
            );
        }
    }

    // The middle third of subject A's decoded depth range on the canvas.
    void measureDepthRange() {
        std::vector<IRMath::uvec2> ids;
        std::vector<int> distances;
        const auto &canvas = mainCanvas();
        IRRender::device()->finish();
        canvas.readEntityIdCarriers(ids);
        canvas.readDistances(distances);
        int lo = 1 << 30;
        int hi = -(1 << 30);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (IRRender::decodeCarrierEntityId(ids[i]) == subjects_[0] &&
                distances[i] < IRPrefab::Glitch::kSubjectEmptyDistance) {
                lo = IRMath::min(lo, distances[i] >> 3);
                hi = IRMath::max(hi, distances[i] >> 3);
            }
        }
        if (lo > hi) {
            IR_LOG_ERROR("[glitch-subject-probe] depth: subject A drew no trixel to measure");
            return;
        }
        const int third = IRMath::max(1, (hi - lo) / 3);
        depthNear_ = lo + third;
        depthFar_ = hi - third;
        depthMeasured_ = true;
        IR_LOG_INFO(
            "[glitch-subject-probe] depth measured subject={} range=[{},{}] interval=[{},{}]",
            subjects_[0],
            lo,
            hi,
            depthNear_,
            depthFar_
        );
    }

    // RENDER, immediately before TRIXEL_GLITCH: the pre-glitch id and
    // distance checksums, from the ticks the capture can land on.
    void preGlitchFrame() {
        if (IRTime::tick() < kPreHashFirstTick) {
            return;
        }
        std::vector<IRMath::uvec2> ids;
        std::vector<int> distances;
        const auto &canvas = mainCanvas();
        IRRender::device()->finish();
        canvas.readEntityIdCarriers(ids);
        canvas.readDistances(distances);
        preIdHash_ = planeHash(ids, distances, preDistanceHash_);
        preHashTick_ = IRTime::tick();
    }

    static std::uint64_t planeHash(
        const std::vector<IRMath::uvec2> &ids,
        const std::vector<int> &distances,
        std::uint64_t &distanceHash
    ) {
        std::uint64_t idHash = 0xCBF29CE484222325ull;
        distanceHash = 0xCBF29CE484222325ull;
        for (const IRMath::uvec2 id : ids) {
            idHash = fnv(fnv(idHash, id.x), id.y);
        }
        for (const int distance : distances) {
            distanceHash = fnv(distanceHash, static_cast<std::uint32_t>(distance));
        }
        return idHash;
    }

    // Every plane the kernel read or wrote this frame.
    struct Planes {
        IRMath::ivec2 size_{0};
        std::vector<IRMath::Color> output_;
        std::vector<IRMath::Color> input_;
        std::vector<IRMath::Color> historyColours_;
        std::vector<IRMath::uvec2> ids_;
        std::vector<IRMath::uvec2> historyIds_;
        std::vector<int> distances_;

        std::size_t index(IRMath::ivec2 q) const {
            return static_cast<std::size_t>(q.y) * static_cast<std::size_t>(size_.x) +
                   static_cast<std::size_t>(q.x);
        }
    };

    static IRMath::uvec4 toUvec4(IRMath::Color colour) {
        return IRMath::uvec4(colour.red_, colour.green_, colour.blue_, colour.alpha_);
    }

    static bool readPlanes(Planes &planes) {
        const auto &canvas = mainCanvas();
        if (!canvas.hasGlitchImages()) {
            return false;
        }
        IRRender::device()->finish();
        planes.size_ = canvas.size_;
        canvas.readColors(planes.output_);
        canvas.readGlitchInputColors(planes.input_);
        canvas.readGlitchHistoryColors(planes.historyColours_);
        canvas.readEntityIdCarriers(planes.ids_);
        canvas.readGlitchHistoryEntityIdCarriers(planes.historyIds_);
        canvas.readDistances(planes.distances_);
        return true;
    }

    // The CPU twin's view of `Planes`.
    struct TwinCanvas {
        const Planes &planes_;
        IRMath::ivec2 size() const {
            return planes_.size_;
        }
        IRMath::uvec4 colour(IRMath::ivec2 q) const {
            return toUvec4(planes_.input_[planes_.index(q)]);
        }
        std::uint64_t id(IRMath::ivec2 q) const {
            return IRRender::decodeCarrierEntityId(planes_.ids_[planes_.index(q)]);
        }
        int distance(IRMath::ivec2 q) const {
            return planes_.distances_[planes_.index(q)];
        }
        IRMath::uvec4 historyColour(IRMath::ivec2 q) const {
            return toUvec4(planes_.historyColours_[planes_.index(q)]);
        }
        std::uint64_t historyId(IRMath::ivec2 q) const {
            return IRRender::decodeCarrierEntityId(planes_.historyIds_[planes_.index(q)]);
        }
    };

    struct Analysis {
        bool valid_ = false;
        int mismatch_ = 0;
        int changed_ = 0;
        int influenced_ = 0;
        int interfered_ = 0;
        int tearPx_ = 0;
        int tearDxMin_ = 0;
        int tearDxMax_ = 0;
        int tearForeign_ = 0;
        int splitPx_ = 0;
        int heldPx_ = 0;
        int heldForeign_ = 0;
        int foreignHistorySamples_ = 0;
        int echoPx_ = 0;
        int dissolvedPx_ = 0;
        int depthInChanged_ = 0;
        int depthOutChanged_ = 0;
        int depthOutUnchangedSubject_ = 0;
        std::uint64_t idHash_ = 0;
        std::uint64_t distanceHash_ = 0;
        std::vector<int> slots_;
        std::vector<std::uint8_t> effect_;
    };

    Analysis analyze(const Planes &planes) const {
        Analysis result;
        const auto *system = IRPrefab::Glitch::trixelSystemOrNull();
        if (system == nullptr || !system->enabledThisFrame_) {
            return result;
        }
        const std::span<const IRMath::uvec4> words{system->words_};
        const IRPrefab::Glitch::TickWords tick =
            IRPrefab::Glitch::toTickWords(system->clock_.tick_);
        const TwinCanvas canvas{planes};
        const IRPrefab::Glitch::SubjectFrame<TwinCanvas> frame{
            canvas,
            system->records_,
            system->state_.seed_,
            tick,
            static_cast<int>(words[1].w),
            words
        };
        const std::size_t texels = planes.output_.size();
        result.slots_.assign(texels, -1);
        result.effect_.assign(texels, 0u);
        IRMath::ivec2 lo = planes.size_;
        IRMath::ivec2 hi(-1);
        for (int y = 0; y < planes.size_.y; ++y) {
            for (int x = 0; x < planes.size_.x; ++x) {
                const int slot = frame.slotAt(IRMath::ivec2(x, y));
                result.slots_[planes.index(IRMath::ivec2(x, y))] = slot;
                if (slot >= 0) {
                    lo = IRMath::min(lo, IRMath::ivec2(x, y));
                    hi = IRMath::max(hi, IRMath::ivec2(x, y));
                }
            }
        }
        const int reach = static_cast<int>(words[1].w);
        lo -= IRMath::ivec2(reach);
        hi += IRMath::ivec2(reach);
        const IRPrefab::Glitch::SubjectRecord *depthRecord =
            mode_ == Mode::DEPTH && !system->records_.empty() ? &system->records_[0] : nullptr;
        result.tearDxMin_ = 1 << 30;
        result.tearDxMax_ = -(1 << 30);
        for (int y = 0; y < planes.size_.y; ++y) {
            for (int x = 0; x < planes.size_.x; ++x) {
                const IRMath::ivec2 p(x, y);
                const std::size_t i = planes.index(p);
                const IRMath::uvec4 input = toUvec4(planes.input_[i]);
                const IRMath::uvec4 output = toUvec4(planes.output_[i]);
                IRPrefab::Glitch::TexelTrace trace;
                IRMath::uvec4 expected = input;
                if (p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y) {
                    expected = frame.texel(p, trace);
                }
                const bool changed = output != input;
                result.mismatch_ += expected != output ? 1 : 0;
                result.changed_ += changed ? 1 : 0;
                result.effect_[i] = changed ? 1u : 0u;
                result.influenced_ += trace.influences_ > 0 ? 1 : 0;
                result.interfered_ += trace.influences_ > 1 ? 1 : 0;
                if (trace.tearDx_ != 0) {
                    ++result.tearPx_;
                    result.tearDxMin_ = IRMath::min(result.tearDxMin_, trace.tearDx_);
                    result.tearDxMax_ = IRMath::max(result.tearDxMax_, trace.tearDx_);
                    // The accepted source, read independently from the id plane.
                    const std::uint64_t source = canvas.id(p + IRMath::ivec2(trace.tearDx_, 0));
                    bool own = false;
                    for (const auto &record : system->records_) {
                        own = own || record.id_ == source;
                    }
                    result.tearForeign_ += own ? 0 : 1;
                }
                result.splitPx_ += trace.splitDx_ != 0 ? 1 : 0;
                result.heldPx_ += trace.held_ ? 1 : 0;
                result.heldForeign_ += trace.heldForeign_ ? 1 : 0;
                result.echoPx_ += trace.echoed_ ? 1 : 0;
                result.dissolvedPx_ += trace.dissolved_ ? 1 : 0;
                // Read independently of the trace: a held texel that shows a
                // history colour whose history id is a non-subject entity.
                const std::uint64_t held = canvas.historyId(p);
                if (trace.held_ && held != 0u && output == canvas.historyColour(p)) {
                    bool subjectHeld = false;
                    for (const auto &record : system->records_) {
                        subjectHeld = subjectHeld || record.id_ == held;
                    }
                    result.foreignHistorySamples_ += subjectHeld ? 0 : 1;
                }
                if (depthRecord != nullptr) {
                    const int distance = planes.distances_[i];
                    const bool inInterval = distance < IRPrefab::Glitch::kSubjectEmptyDistance &&
                                            (distance >> 3) >= depthRecord->depthNear_ &&
                                            (distance >> 3) <= depthRecord->depthFar_;
                    if (changed) {
                        ++(inInterval ? result.depthInChanged_ : result.depthOutChanged_);
                    } else if (!inInterval && result.slots_[i] == 0) {
                        ++result.depthOutUnchangedSubject_;
                    }
                }
            }
        }
        if (result.tearPx_ == 0) {
            result.tearDxMin_ = 0;
            result.tearDxMax_ = 0;
        }
        result.idHash_ = planeHash(planes.ids_, planes.distances_, result.distanceHash_);
        result.valid_ = true;
        return result;
    }

    bool modePasses(const Analysis &analysis, bool perAxis) const {
        if (!analysis.valid_ || analysis.mismatch_ != 0 || perAxis) {
            return false;
        }
        switch (mode_) {
        case Mode::TEAR:
            return analysis.tearPx_ > 0 && analysis.tearForeign_ == 0 && analysis.changed_ > 0;
        case Mode::HOLD:
            return analysis.heldPx_ > 0 && analysis.heldForeign_ == 0 &&
                   analysis.foreignHistorySamples_ == 0 && analysis.changed_ > 0;
        case Mode::SPLIT:
            return analysis.splitPx_ > 0 && analysis.changed_ > 0;
        case Mode::ECHO:
            return analysis.echoPx_ > 0 && analysis.changed_ > 0;
        case Mode::DISSOLVE:
            return analysis.dissolvedPx_ > 0 && analysis.idHash_ == preIdHash_ &&
                   analysis.distanceHash_ == preDistanceHash_ && preHashTick_ == IRTime::tick();
        case Mode::DEPTH:
            return analysis.depthInChanged_ > 0 && analysis.depthOutChanged_ == 0 &&
                   analysis.depthOutUnchangedSubject_ > 0;
        default:
            return analysis.changed_ > 0;
        }
    }

    std::string subjectIds() const {
        std::string ids;
        for (int i = 0; i < subjectCount_; ++i) {
            ids += (i == 0 ? "" : ",") + std::to_string(subjects_[static_cast<std::size_t>(i)]);
        }
        return ids;
    }

    void logSystemLine(const char *when) const {
        const auto *system = IRPrefab::Glitch::trixelSystemOrNull();
        if (system == nullptr) {
            return;
        }
        const IRPrefab::Glitch::Clock clock = IRPrefab::Glitch::clock();
        const auto words = IRPrefab::Glitch::toTickWords(clock.tick_);
        const auto *framebuffer = IRPrefab::Glitch::systemOrNull();
        IR_LOG_INFO(
            "[glitch-subject-probe] at={} mode={} engineTick={} tick={} tickWords={},{} "
            "trixelTick={} enabled={} subjects=[{}] perAxisAllocated={} frameUploads={} "
            "frameHistoryCopies={} frameDispatches={} frameSwaps={} totalDispatches={} "
            "totalHistoryCopies={} imageCreations={} fbFrameDraws={} gpuMs={:.4f}",
            when,
            static_cast<int>(mode_),
            IRTime::tick(),
            clock.tick_,
            words.low_,
            words.high_,
            system->clock_.tick_,
            system->enabledThisFrame_,
            subjectIds(),
            system->perAxisAllocated_,
            system->frameUploads_,
            system->frameHistoryCopies_,
            system->frameDispatches_,
            system->frameSwaps_,
            system->dispatchCount_,
            system->historyCopyCount_,
            system->imageCreationCount_,
            framebuffer != nullptr ? framebuffer->frameDraws_ : -1,
            IRRender::gpuStageTiming().trixelGlitchMs_
        );
    }

    void logCapture(int shotIndex) {
        logSystemLine("capture");
        const auto *system = IRPrefab::Glitch::trixelSystemOrNull();
        if (mode_ == Mode::CHOREOGRAPHY) {
            const auto *framebuffer = IRPrefab::Glitch::systemOrNull();
            const std::int64_t tick = IRPrefab::Glitch::clock().tick_;
            const bool wantSubject = shotIndex == 0;
            const bool parity = ((tick & 1) == 0) == wantSubject;
            const bool fired = wantSubject
                                   ? system->frameDispatches_ == 1 && framebuffer->frameDraws_ == 0
                                   : system->frameDispatches_ == 0 && framebuffer->frameDraws_ == 1;
            const bool sameTick = system->clock_.tick_ == tick;
            IR_LOG_INFO(
                "[glitch-subject-probe] choreography shot={} tick={} expects={} parity={} "
                "fired={} sameTickBothPasses={} pass={}",
                shotIndex,
                tick,
                wantSubject ? "subject" : "framebuffer-tear",
                parity,
                fired,
                sameTick,
                parity && fired && sameTick
            );
            if (!wantSubject) {
                return;
            }
        }
        if (system == nullptr || !system->enabledThisFrame_) {
            IR_LOG_INFO(
                "[glitch-subject-probe] capture dispatches={} (no active subject) pass={}",
                system != nullptr ? system->frameDispatches_ : -1,
                mode_ == Mode::OFF || mode_ == Mode::SPILL_FAR_OFF || mode_ == Mode::SPILL_NEAR_OFF
            );
            return;
        }
        Planes planes;
        if (!readPlanes(planes)) {
            IR_LOG_ERROR("[glitch-subject-probe] capture: main canvas has no glitch images");
            return;
        }
        const Analysis analysis = analyze(planes);
        IR_LOG_INFO(
            "[glitch-subject-probe] canvas size={}x{} cpuMismatch={} changed={} influenced={} "
            "interfered={} tearPx={} tearDx=[{},{}] tearForeign={} splitPx={} heldPx={} "
            "heldForeign={} foreignHistorySamples={} echoPx={} dissolvedPx={} "
            "idHash={:016x}/{:016x} distanceHash={:016x}/{:016x} pass={}",
            planes.size_.x,
            planes.size_.y,
            analysis.mismatch_,
            analysis.changed_,
            analysis.influenced_,
            analysis.interfered_,
            analysis.tearPx_,
            analysis.tearDxMin_,
            analysis.tearDxMax_,
            analysis.tearForeign_,
            analysis.splitPx_,
            analysis.heldPx_,
            analysis.heldForeign_,
            analysis.foreignHistorySamples_,
            analysis.echoPx_,
            analysis.dissolvedPx_,
            preIdHash_,
            analysis.idHash_,
            preDistanceHash_,
            analysis.distanceHash_,
            modePasses(analysis, system->perAxisAllocated_)
        );
        if (mode_ == Mode::DEPTH) {
            IR_LOG_INFO(
                "[glitch-subject-probe] depth subject={} interval=[{},{}] inChanged={} "
                "outChanged={} outUnchangedSubject={}",
                subjects_[0],
                depthNear_,
                depthFar_,
                analysis.depthInChanged_,
                analysis.depthOutChanged_,
                analysis.depthOutUnchangedSubject_
            );
        }
        if (isSpill()) {
            logSpillOracle();
        }
    }

    // RENDER, right after TRIXEL_GLITCH: the spill modes' far and near reads.
    void postGlitchFrame() {
        if (!isSpill()) {
            return;
        }
        const std::uint64_t tick = IRTime::tick();
        SpillPose *pose = tick == kSpillFarStart + kSpillPoseTicks - 1    ? &far_
                          : tick == kSpillNearStart + kSpillPoseTicks - 1 ? &near_
                                                                          : nullptr;
        if (pose == nullptr) {
            return;
        }
        logSystemLine(pose == &far_ ? "spill-far" : "spill-near");
        Planes planes;
        if (!readPlanes(planes)) {
            return;
        }
        Analysis analysis = analyze(planes);
        if (!analysis.valid_) {
            return;
        }
        IR_LOG_INFO(
            "[glitch-subject-probe] spill pose={} cpuMismatch={} changed={} interfered={}",
            pose == &far_ ? "far" : "near",
            analysis.mismatch_,
            analysis.changed_,
            analysis.interfered_
        );
        pose->valid_ = analysis.mismatch_ == 0;
        pose->size_ = planes.size_;
        pose->slots_ = std::move(analysis.slots_);
        pose->effect_ = std::move(analysis.effect_);
    }

    // Chebyshev dilation of slot @p slot's texels by @p radius.
    static std::vector<std::uint8_t> band(const SpillPose &pose, int slot, int radius) {
        const auto index = [&pose](int x, int y) {
            return static_cast<std::size_t>(y) * static_cast<std::size_t>(pose.size_.x) +
                   static_cast<std::size_t>(x);
        };
        std::vector<std::uint8_t> out(pose.slots_.size(), 0u);
        for (int y = 0; y < pose.size_.y; ++y) {
            for (int x = 0; x < pose.size_.x; ++x) {
                if (pose.slots_[index(x, y)] != slot) {
                    continue;
                }
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int qx = x + dx;
                        const int qy = y + dy;
                        if (qx >= 0 && qy >= 0 && qx < pose.size_.x && qy < pose.size_.y) {
                            out[index(qx, qy)] = 1u;
                        }
                    }
                }
            }
        }
        return out;
    }

    // Far and near effect masks: subject B moves by a whole-trixel canvas
    // shift D between them. Far effects attribute to the subject whose band
    // holds them (the far bands are disjoint); B's part is shifted by D. The
    // XOR against the near mask must be zero outside the near pair-overlap
    // band and positive inside it.
    void logSpillOracle() const {
        if (!far_.valid_ || !near_.valid_ || far_.size_ != near_.size_) {
            IR_LOG_INFO("[glitch-subject-probe] spill oracle unavailable pass=false");
            return;
        }
        const int spill = IRPrefab::Glitch::kSubjectMaxSpill;
        const IRMath::ivec2 size = far_.size_;
        const auto index = [&size](IRMath::ivec2 q) {
            return static_cast<std::size_t>(q.y) * static_cast<std::size_t>(size.x) +
                   static_cast<std::size_t>(q.x);
        };
        // D from B's bounding boxes, then checked texel for texel.
        IRMath::ivec2 farMin = size;
        IRMath::ivec2 nearMin = size;
        int farCount = 0;
        int nearCount = 0;
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                if (far_.slots_[index(IRMath::ivec2(x, y))] == 1) {
                    farMin = IRMath::min(farMin, IRMath::ivec2(x, y));
                    ++farCount;
                }
                if (near_.slots_[index(IRMath::ivec2(x, y))] == 1) {
                    nearMin = IRMath::min(nearMin, IRMath::ivec2(x, y));
                    ++nearCount;
                }
            }
        }
        const IRMath::ivec2 shift = nearMin - farMin;
        int shiftMismatch = farCount == nearCount ? 0 : 1;
        const auto inside = [&size](IRMath::ivec2 q) {
            return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
        };
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const IRMath::ivec2 q(x, y);
                if (far_.slots_[index(q)] == 1) {
                    const IRMath::ivec2 moved = q + shift;
                    shiftMismatch += inside(moved) && near_.slots_[index(moved)] == 1 ? 0 : 1;
                }
            }
        }
        const auto farA = band(far_, 0, spill);
        const auto farB = band(far_, 1, spill);
        const auto nearA = band(near_, 0, spill);
        const auto nearB = band(near_, 1, spill);
        int farOverlap = 0;
        int nearOverlap = 0;
        std::vector<std::uint8_t> expected(far_.effect_.size(), 0u);
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const IRMath::ivec2 q(x, y);
                const std::size_t i = index(q);
                farOverlap += farA[i] != 0u && farB[i] != 0u ? 1 : 0;
                nearOverlap += nearA[i] != 0u && nearB[i] != 0u ? 1 : 0;
                if (far_.effect_[i] == 0u) {
                    continue;
                }
                if (farB[i] != 0u) {
                    const IRMath::ivec2 moved = q + shift;
                    if (inside(moved)) {
                        expected[index(moved)] = 1u;
                    }
                } else {
                    expected[i] = 1u;
                }
            }
        }
        int xorInside = 0;
        int xorOutside = 0;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (expected[i] == near_.effect_[i]) {
                continue;
            }
            ++(nearA[i] != 0u && nearB[i] != 0u ? xorInside : xorOutside);
        }
        const bool pass = shiftMismatch == 0 && farOverlap == 0 && nearOverlap > 0 &&
                          xorOutside == 0 && xorInside > 0;
        IR_LOG_INFO(
            "[glitch-subject-probe] spill oracle subjects=[{}] spill={} moverShift=({},{}) "
            "moverShiftMismatch={} farOverlapPx={} nearOverlapPx={} xorOutsideOverlap={} "
            "xorInsideOverlap={} pass={}",
            subjectIds(),
            spill,
            shift.x,
            shift.y,
            shiftMismatch,
            farOverlap,
            nearOverlap,
            xorOutside,
            xorInside,
            pass
        );
    }
};

} // namespace IRShapeDebug

#endif /* GLITCH_SUBJECT_FIXTURE_H */
