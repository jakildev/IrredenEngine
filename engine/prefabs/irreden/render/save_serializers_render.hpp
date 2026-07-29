#ifndef IR_SAVE_SERIALIZERS_RENDER_H
#define IR_SAVE_SERIALIZERS_RENDER_H

/// `SaveSerialize<C>` specializations for the heap-owning components in
/// `engine/prefabs/irreden/render/` (#2242) — text, sprite playback state,
/// the triangle-canvas pair, and the widget family.
///
/// These are all **CPU-side** render components. The GPU-handle-owning ones
/// (`C_TrixelCanvasFramebuffer`, `C_TriangleCanvasTextures`, the canvas
/// texture bundle) are opted OUT of persistence entirely in
/// `save_component_inventory.hpp` Class A and never reach a serializer.
///
/// Opt-in serializer header: include it wherever a registry registers these
/// components; never pulled by the component headers themselves.

#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_triangles_only_set.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/math_binary_io.hpp>

#include <irreden/ir_profile.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace IRWorld {

template <> struct SaveSerialize<IRComponents::C_TextSegment> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_TextSegment &value) {
        w.writeString(value.text_);
    }

    static IRAsset::Result<IRComponents::C_TextSegment> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_TextSegment>;
        IRComponents::C_TextSegment value{};
        IR_SAVE_READ(value.text_, r.readString());
        return Res::success(std::move(value));
    }
};

/// `sheetEntity_` is an `EntityId` and round-trips as a plain value under the
/// snapshot's exact-id restore contract. `animationIndex_` is an index into
/// the *sheet's* animation vector, resolved at `playAnimation` time; the
/// component header already documents it as silently stale if the sheet
/// changes underneath, and a reload is no different from that case.
template <> struct SaveSerialize<IRComponents::C_SpriteAnimation> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_SpriteAnimation &value) {
        w.writeU64(static_cast<std::uint64_t>(value.sheetEntity_));
        w.writeI32(value.animationIndex_);
        w.writeI32(value.frameIndex_);
        w.writeF32(value.elapsedInFrame_);
        w.writeI32(static_cast<std::int32_t>(value.loopMode_));
        w.writeF32(value.speed_);
        w.writeI32(value.pingPongDirection_);
        w.writeU8(value.terminated_ ? 1 : 0);
        w.writeU8(value.stopped_ ? 1 : 0);
        w.writeString(value.currentAnimName_);
    }

    static IRAsset::Result<IRComponents::C_SpriteAnimation> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_SpriteAnimation>;
        IRComponents::C_SpriteAnimation value{};

        std::uint64_t sheetEntity = 0;
        IR_SAVE_READ(sheetEntity, r.readU64());
        value.sheetEntity_ = static_cast<IREntity::EntityId>(sheetEntity);

        IR_SAVE_READ(value.animationIndex_, r.readI32());
        IR_SAVE_READ(value.frameIndex_, r.readI32());
        IR_SAVE_READ(value.elapsedInFrame_, r.readF32());

        std::int32_t loopMode = 0;
        IR_SAVE_READ(loopMode, r.readI32());
        value.loopMode_ = static_cast<IRComponents::SpriteLoopMode>(loopMode);

        IR_SAVE_READ(value.speed_, r.readF32());
        IR_SAVE_READ(value.pingPongDirection_, r.readI32());
        IR_SAVE_READ_BOOL(value.terminated_, r.readU8());
        IR_SAVE_READ_BOOL(value.stopped_, r.readU8());
        IR_SAVE_READ(value.currentAnimName_, r.readString());
        return Res::success(std::move(value));
    }
};

/// The two parallel grids are the authored content; `size_` gives their
/// expected extent. `read` restores the vectors verbatim rather than calling
/// `resize()` off `size_` — a mismatch is **rejected**, never truncated or
/// zero-padded into looking valid, and never passed through: the
/// `atTriangleColor` / `atTriangleDistance` accessors index
/// `triangleColors_[index2DtoIndex1D(index, size_)]` unchecked and
/// `setTriangle`'s lone bounds check is an `IR_ASSERT` that compiles out
/// under `IR_RELEASE`, so an inconsistent grid is an out-of-bounds access on
/// first use rather than visibly-wrong data. Rejecting holds the reader to the
/// contract the constructors establish —
/// `triangleColors_.size() == triangleDistances_.size() == size_.x*size_.y`,
/// the same shape `SaveSerialize<C_Cycle>` enforces on its breakpoint count.
/// The reader is deliberately **tighter** than the constructors in one respect:
/// it also rejects a negative dimension. Both the two-argument constructor and
/// `resize()` admit one — each sizes the grids with an unguarded
/// `resize(size_.x * size_.y)`, and a both-negative pair multiplies to a
/// positive cell count, so the grids come out plausibly sized — but the
/// resulting extent breaks the indexing math the accessors rely on. The
/// component's own invariant is weaker here than the accessors need, so the
/// reader does not widen to match it.
template <> struct SaveSerialize<IRComponents::C_TrianglesOnlySet> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_TrianglesOnlySet &value) {
        // Debug-only mirror of `read`'s guard. Without it the fault surfaces
        // one save/load cycle later as a corrupt-file error, by which point the
        // entity that held the bad extent is gone; here the culprit is still
        // live. `read` remains the enforcing check — this compiles out under
        // `IR_RELEASE`.
        IR_ASSERT(
            value.size_.x >= 0 && value.size_.y >= 0 &&
                static_cast<std::int64_t>(value.triangleColors_.size()) ==
                    static_cast<std::int64_t>(value.size_.x) *
                        static_cast<std::int64_t>(value.size_.y) &&
                value.triangleDistances_.size() == value.triangleColors_.size(),
            "C_TrianglesOnlySet: saving grids ({} colors, {} distances) that disagree with the "
            "extent {}x{}; read will reject this file",
            value.triangleColors_.size(),
            value.triangleDistances_.size(),
            value.size_.x,
            value.size_.y
        );
        IRMath::BinaryIO::writeIVec2(w, value.size_);
        IRMath::BinaryIO::writeIVec2(w, value.origin_);
        detail::writeTrivialVector(w, value.triangleColors_);
        detail::writeTrivialVector(w, value.triangleDistances_);
    }

    static IRAsset::Result<IRComponents::C_TrianglesOnlySet> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_TrianglesOnlySet>;
        IRComponents::C_TrianglesOnlySet value{};
        IR_SAVE_READ(value.size_, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ(value.origin_, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.triangleColors_));
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.triangleDistances_));

        // Two independent faults, so two checks with their own messages: a
        // corrupt extent and a cardinality mismatch point at different repairs,
        // and a shared message mis-names whichever one it wasn't written for
        // (a both-negative extent reads as "6 and 6 disagree with -2x-3", which
        // is self-contradicting — -2 * -3 predicts exactly 6).
        //
        // Sign first. A mixed-sign pair gives a negative product, but a
        // both-negative pair gives a POSITIVE one ((-2,-3) -> +6), so a
        // product-only test admits it — and `index2DtoIndex1D`
        // (`index.y * size_.x + index.x`) then goes negative for any row past
        // the first, indexing out of bounds in the unchecked `atTriangleColor`
        // / `atTriangleDistance` accessors.
        if (value.size_.x < 0 || value.size_.y < 0) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_TrianglesOnlySet: negative extent " + std::to_string(value.size_.x) + "x" +
                    std::to_string(value.size_.y) + " is not indexable"
            );
        }

        // Then cardinality. Widened to 64-bit because both dimensions come off
        // disk: the check above bounds them at >= 0, but a corrupt pair still
        // overflows the component's own `int` multiply.
        const std::int64_t expectedCells =
            static_cast<std::int64_t>(value.size_.x) * static_cast<std::int64_t>(value.size_.y);
        if (static_cast<std::int64_t>(value.triangleColors_.size()) != expectedCells ||
            static_cast<std::int64_t>(value.triangleDistances_.size()) != expectedCells) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_TrianglesOnlySet: grid sizes (" + std::to_string(value.triangleColors_.size()) +
                    " colors, " + std::to_string(value.triangleDistances_.size()) +
                    " distances) disagree with the extent " + std::to_string(value.size_.x) + "x" +
                    std::to_string(value.size_.y)
            );
        }
        return Res::success(std::move(value));
    }
};

/// Authored state only. `C_TriangleCanvasBackground` keeps two per-frame
/// caches — `m_randomColorData` (re-randomized every frame in
/// kGradientRandom, fully rewritten every pulse frame in kPulsePattern) and
/// `m_patternMask` (recomputed whenever `m_patternMaskInitialized` is false)
/// — plus the bookkeeping that drives them. None of that is authored truth,
/// so `read` routes the authored fields through the constructor (which sizes
/// both caches for `m_size`) and leaves `m_patternMaskInitialized` at its
/// default `false` so the next frame rebuilds the mask for the restored zoom.
///
/// This specialization is a friend of the component; see the friend
/// declaration in `component_triangle_canvas_background.hpp` for why the
/// private fields are reached directly rather than through accessors.
template <> struct SaveSerialize<IRComponents::C_TriangleCanvasBackground> {
    static void
    write(IRAsset::BinaryWriter &w, const IRComponents::C_TriangleCanvasBackground &value) {
        w.writeI32(static_cast<std::int32_t>(value.m_type));
        IRMath::BinaryIO::writeIVec2(w, value.m_size);
        detail::writeTrivialVector(w, value.m_colors);
        w.writeF32(value.m_pulseSpeed);
        w.writeI32(value.m_patternScale);

        // Live animation phase — genuinely stateful, not derived: it
        // accumulates across frames and defines where in the pulse the
        // canvas currently is.
        w.writeF32(value.m_pulsePhase);
        w.writeF32(value.m_patternZoomMultiplier);

        IRMath::BinaryIO::writeVec2(w, value.m_pulseWaveDirection);
        w.writeF32(value.m_pulseWavePhaseScale);
        IRMath::BinaryIO::writeVec2(w, value.m_pulseWaveDirectionSecondary);
        w.writeF32(value.m_pulseWavePhaseScaleSecondary);
        w.writeF32(value.m_pulseWaveInterferenceMix);
        w.writeF32(value.m_pulseWavePrimarySpeedMultiplier);
        w.writeF32(value.m_pulseWavePrimaryStartOffset);
        w.writeF32(value.m_pulseWaveSecondarySpeedMultiplier);
        w.writeF32(value.m_pulseWaveSecondaryStartOffset);
        w.writeF32(value.m_minPulseUpdateSeconds);

        writeDirectionMotion(w, value.m_pulseWaveDirectionMotionPrimary);
        writeDirectionMotion(w, value.m_pulseWaveDirectionMotionSecondary);
    }

    static IRAsset::Result<IRComponents::C_TriangleCanvasBackground>
    read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_TriangleCanvasBackground>;

        std::int32_t type = 0;
        IRMath::ivec2 size{0, 0};
        std::vector<IRMath::Color> colors;
        float pulseSpeed = 1.0f;
        std::int32_t patternScale = 10;

        IR_SAVE_READ(type, r.readI32());
        IR_SAVE_READ(size, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, colors));
        IR_SAVE_READ(pulseSpeed, r.readF32());
        IR_SAVE_READ(patternScale, r.readI32());

        // Through the constructor so both per-frame caches come back sized
        // for `size` by the code that owns that relationship.
        IRComponents::C_TriangleCanvasBackground value{
            static_cast<IRComponents::BackgroundTypes>(type),
            colors,
            size,
            pulseSpeed,
            patternScale
        };

        IR_SAVE_READ(value.m_pulsePhase, r.readF32());
        IR_SAVE_READ(value.m_patternZoomMultiplier, r.readF32());

        IR_SAVE_READ(value.m_pulseWaveDirection, IRMath::BinaryIO::readVec2(r));
        IR_SAVE_READ(value.m_pulseWavePhaseScale, r.readF32());
        IR_SAVE_READ(value.m_pulseWaveDirectionSecondary, IRMath::BinaryIO::readVec2(r));
        IR_SAVE_READ(value.m_pulseWavePhaseScaleSecondary, r.readF32());
        IR_SAVE_READ(value.m_pulseWaveInterferenceMix, r.readF32());
        IR_SAVE_READ(value.m_pulseWavePrimarySpeedMultiplier, r.readF32());
        IR_SAVE_READ(value.m_pulseWavePrimaryStartOffset, r.readF32());
        IR_SAVE_READ(value.m_pulseWaveSecondarySpeedMultiplier, r.readF32());
        IR_SAVE_READ(value.m_pulseWaveSecondaryStartOffset, r.readF32());
        IR_SAVE_READ(value.m_minPulseUpdateSeconds, r.readF32());

        IR_SAVE_READ_STATUS(readDirectionMotion(r, value.m_pulseWaveDirectionMotionPrimary));
        IR_SAVE_READ_STATUS(readDirectionMotion(r, value.m_pulseWaveDirectionMotionSecondary));
        return Res::success(std::move(value));
    }

  private:
    using DirectionMotion = IRComponents::C_TriangleCanvasBackground::LinearDirectionMotion;

    static void writeDirectionMotion(IRAsset::BinaryWriter &w, const DirectionMotion &motion) {
        w.writeU8(motion.enabled_ ? 1 : 0);
        w.writeF32(motion.timeSeconds_);
        w.writeF32(motion.periodSeconds_);
        IRMath::BinaryIO::writeVec2(w, motion.startDirection_);
        IRMath::BinaryIO::writeVec2(w, motion.endDirection_);
        w.writeI32(static_cast<std::int32_t>(motion.forwardEasing_));
        w.writeI32(static_cast<std::int32_t>(motion.backwardEasing_));
    }

    // Reports a bare status rather than a Result so the caller can use the
    // same IR_SAVE_READ_STATUS bail-out the container helpers use.
    static IRAsset::BinaryStatus
    readDirectionMotion(IRAsset::BinaryReader &r, DirectionMotion &motion) {
        IRAsset::Result<std::uint8_t> enabled = r.readU8();
        if (!enabled.ok()) {
            return enabled.status_;
        }
        IRAsset::Result<float> timeSeconds = r.readF32();
        if (!timeSeconds.ok()) {
            return timeSeconds.status_;
        }
        IRAsset::Result<float> periodSeconds = r.readF32();
        if (!periodSeconds.ok()) {
            return periodSeconds.status_;
        }
        IRAsset::Result<IRMath::vec2> startDirection = IRMath::BinaryIO::readVec2(r);
        if (!startDirection.ok()) {
            return startDirection.status_;
        }
        IRAsset::Result<IRMath::vec2> endDirection = IRMath::BinaryIO::readVec2(r);
        if (!endDirection.ok()) {
            return endDirection.status_;
        }
        IRAsset::Result<std::int32_t> forwardEasing = r.readI32();
        if (!forwardEasing.ok()) {
            return forwardEasing.status_;
        }
        IRAsset::Result<std::int32_t> backwardEasing = r.readI32();
        if (!backwardEasing.ok()) {
            return backwardEasing.status_;
        }

        motion.enabled_ = enabled.value_ != 0;
        motion.timeSeconds_ = timeSeconds.value_;
        motion.periodSeconds_ = periodSeconds.value_;
        motion.startDirection_ = startDirection.value_;
        motion.endDirection_ = endDirection.value_;
        motion.forwardEasing_ = static_cast<IREasingFunctions>(forwardEasing.value_);
        motion.backwardEasing_ = static_cast<IREasingFunctions>(backwardEasing.value_);
        return IRAsset::BinaryStatus::success();
    }
};

// ---------------------------------------------------------------------------
// Widget family. Every widget entity carries C_Widget + C_WidgetState (both
// trivially copyable, so they take SaveSerialize's fast path) plus one
// per-kind data component. Only the per-kind components that own a string or
// a vector of strings need a specialization here; C_WidgetColorSwatch and
// C_WidgetScroll are plain data and are covered by the fast path.
// ---------------------------------------------------------------------------

template <> struct SaveSerialize<IRComponents::C_WidgetPanel> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetPanel &value) {
        w.writeString(value.title_);
        w.writeU8(value.drawBorder_ ? 1 : 0);
    }

    static IRAsset::Result<IRComponents::C_WidgetPanel> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetPanel>;
        IRComponents::C_WidgetPanel value{};
        IR_SAVE_READ(value.title_, r.readString());
        IR_SAVE_READ_BOOL(value.drawBorder_, r.readU8());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetLabel> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetLabel &value) {
        w.writeString(value.text_);
        w.writeU32(value.colorOverride_.toPackedRGBA());
    }

    static IRAsset::Result<IRComponents::C_WidgetLabel> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetLabel>;
        IRComponents::C_WidgetLabel value{};
        IR_SAVE_READ(value.text_, r.readString());
        std::uint32_t packed = 0;
        IR_SAVE_READ(packed, r.readU32());
        value.colorOverride_ = IRMath::Color::fromPackedRGBA(packed);
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetButton> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetButton &value) {
        w.writeString(value.label_);
    }

    static IRAsset::Result<IRComponents::C_WidgetButton> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetButton>;
        IRComponents::C_WidgetButton value{};
        IR_SAVE_READ(value.label_, r.readString());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetSlider> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetSlider &value) {
        w.writeString(value.label_);
        w.writeF32(value.minValue_);
        w.writeF32(value.maxValue_);
        w.writeF32(value.currentValue_);
    }

    static IRAsset::Result<IRComponents::C_WidgetSlider> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetSlider>;
        IRComponents::C_WidgetSlider value{};
        IR_SAVE_READ(value.label_, r.readString());
        IR_SAVE_READ(value.minValue_, r.readF32());
        IR_SAVE_READ(value.maxValue_, r.readF32());
        IR_SAVE_READ(value.currentValue_, r.readF32());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetCheckbox> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetCheckbox &value) {
        w.writeString(value.label_);
        w.writeU8(value.checked_ ? 1 : 0);
    }

    static IRAsset::Result<IRComponents::C_WidgetCheckbox> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetCheckbox>;
        IRComponents::C_WidgetCheckbox value{};
        IR_SAVE_READ(value.label_, r.readString());
        IR_SAVE_READ_BOOL(value.checked_, r.readU8());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetList> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetList &value) {
        detail::writeStringVector(w, value.items_);
        w.writeI32(value.selectedIndex_);
        w.writeI32(value.scrollOffset_);
        w.writeI32(value.itemHeight_);
    }

    static IRAsset::Result<IRComponents::C_WidgetList> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetList>;
        IRComponents::C_WidgetList value{};
        IR_SAVE_READ_STATUS(detail::readStringVector(r, value.items_));
        IR_SAVE_READ(value.selectedIndex_, r.readI32());
        IR_SAVE_READ(value.scrollOffset_, r.readI32());
        IR_SAVE_READ(value.itemHeight_, r.readI32());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetDropdown> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetDropdown &value) {
        detail::writeStringVector(w, value.items_);
        w.writeI32(value.selectedIndex_);
        w.writeU8(value.isOpen_ ? 1 : 0);
        w.writeI32(value.itemHeight_);
    }

    static IRAsset::Result<IRComponents::C_WidgetDropdown> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetDropdown>;
        IRComponents::C_WidgetDropdown value{};
        IR_SAVE_READ_STATUS(detail::readStringVector(r, value.items_));
        IR_SAVE_READ(value.selectedIndex_, r.readI32());
        IR_SAVE_READ_BOOL(value.isOpen_, r.readU8());
        IR_SAVE_READ(value.itemHeight_, r.readI32());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetRadio> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetRadio &value) {
        w.writeString(value.label_);
        w.writeU32(value.groupId_);
        w.writeI32(value.value_);
        w.writeU8(value.selected_ ? 1 : 0);
    }

    static IRAsset::Result<IRComponents::C_WidgetRadio> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetRadio>;
        IRComponents::C_WidgetRadio value{};
        IR_SAVE_READ(value.label_, r.readString());
        IR_SAVE_READ(value.groupId_, r.readU32());
        IR_SAVE_READ(value.value_, r.readI32());
        IR_SAVE_READ_BOOL(value.selected_, r.readU8());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetTextInput> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetTextInput &value) {
        w.writeString(value.text_);
        w.writeI32(value.cursorPos_);
        w.writeI32(value.maxLength_);
    }

    static IRAsset::Result<IRComponents::C_WidgetTextInput> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetTextInput>;
        IRComponents::C_WidgetTextInput value{};
        IR_SAVE_READ(value.text_, r.readString());
        IR_SAVE_READ(value.cursorPos_, r.readI32());
        IR_SAVE_READ(value.maxLength_, r.readI32());
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_RENDER_H */
