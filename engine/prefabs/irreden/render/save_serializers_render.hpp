#ifndef IR_SAVE_SERIALIZERS_RENDER_H
#define IR_SAVE_SERIALIZERS_RENDER_H

/// `SaveSerialize<C>` specializations for components in
/// `engine/prefabs/irreden/render/` that need hand-written layouts — text,
/// sprite playback state, entity canvases, the triangle-canvas pair, and the
/// widget family.
///
/// These are all **CPU-side** render components. The GPU-handle-owning ones
/// (`C_TrixelCanvasFramebuffer`, `C_TriangleCanvasTextures`, the canvas
/// texture bundle) are opted OUT of persistence entirely in
/// `save_component_inventory.hpp` Class A and never reach a serializer.
///
/// Opt-in serializer header: include it wherever a registry registers these
/// components; never pulled by the component headers themselves.

#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_triangles_only_set.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_image.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>
#include <irreden/world/save_migration.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/math_binary_io.hpp>

#include <irreden/ir_profile.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace IRWorld {

namespace detail {

struct EntityCanvasV1 {
    IREntity::EntityId canvasEntity_;
    IRMath::ivec2 canvasSize_;
    bool visible_;
    bool screenLocked_;
    int depthPriority_;
};

static_assert(sizeof(EntityCanvasV1) == 24);

struct EntityCanvasV2 {
    IREntity::EntityId canvasEntity_;
    IRMath::ivec2 canvasSize_;
    bool visible_;
    bool screenLocked_;
    int depthPriority_;
    float fogRevealFactor_;
    bool fogHidden_;
};

static_assert(sizeof(EntityCanvasV2) == 32);

template <typename LegacyCanvas>
IRAsset::Result<IRComponents::C_EntityCanvas>
readEntityCanvasLegacy(IRAsset::BinaryReader &reader) {
    LegacyCanvas old{};
    IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
    if (!status.ok()) {
        return IRAsset::Result<IRComponents::C_EntityCanvas>::error(
            status.code_,
            std::move(status.message_)
        );
    }

    return IRAsset::Result<IRComponents::C_EntityCanvas>::success(
        IRComponents::C_EntityCanvas{
            old.canvasEntity_,
            old.canvasSize_,
            old.visible_,
            old.screenLocked_,
            old.depthPriority_,
        }
    );
}

// An empty marker: its raw image is the one byte an empty struct occupies,
// with no defined value.
struct CameraV1 {};

static_assert(sizeof(CameraV1) == 1);

struct GizmoHandleV1 {
    IRComponents::GizmoKind kind_;
    IRComponents::GizmoAxis axis_;
    bool hover_;
    bool isAnchor_;
    IRMath::vec4 referenceParams_;
    IRMath::vec3 referenceLocalPos_;
    IRMath::Color baseColor_;
    IREntity::EntityId anchorEntity_;
};

static_assert(sizeof(GizmoHandleV1) == 48);

struct TextStyleV1 {
    IRMath::Color color_;
    int wrapWidth_;
    IRComponents::TextAlignH alignH_;
    IRComponents::TextAlignV alignV_;
    int boxWidth_;
    int boxHeight_;
    int fontSize_;
};

static_assert(sizeof(TextStyleV1) == 28);

// The version-1 raw image: three padding bytes follow `disabled_`.
struct WidgetV1 {
    IRComponents::WidgetKind kind_;
    IRMath::ivec2 size_;
    bool disabled_;
    int zOrder_;
};

static_assert(sizeof(WidgetV1) == 20);

struct WidgetStateV1 {
    bool hovered_;
    bool pressed_;
    bool focused_;
    bool fireAction_;
    float dragValue_;
};

static_assert(sizeof(WidgetStateV1) == 8);

// An enum whose enumerators run 0..`last` is one byte on disk. Its `int`
// underlying type admits values no enumerator names, so `read` rejects them:
// the text draws index the glyph sets and alignment offsets by these values.
template <typename E> bool enumInRange(E value, E last) {
    const int raw = static_cast<int>(value);
    return raw >= 0 && raw <= static_cast<int>(last);
}

template <typename E>
void writeEnumByte(IRAsset::BinaryWriter &w, E value, E last, const char *field) {
    IR_ASSERT(
        enumInRange(value, last),
        "{}: saving {}, which read will reject",
        field,
        static_cast<int>(value)
    );
    w.writeU8(static_cast<std::uint8_t>(value));
}

template <typename E>
IRAsset::BinaryStatus readEnumByte(IRAsset::BinaryReader &r, E last, const char *field, E &out) {
    IRAsset::Result<std::uint8_t> raw = r.readU8();
    if (!raw.ok()) {
        return raw.status_;
    }
    const E value = static_cast<E>(raw.value_);
    if (!enumInRange(value, last)) {
        return IRAsset::BinaryStatus::error(
            IRAsset::BinaryIOError::UnknownTag,
            std::string(field) + " " + std::to_string(raw.value_) + " is outside [0, " +
                std::to_string(static_cast<int>(last)) + "]"
        );
    }
    out = value;
    return IRAsset::BinaryStatus::success();
}

inline void writeTextSize(IRAsset::BinaryWriter &w, IRRender::TextSize size, const char *field) {
    writeEnumByte(w, size, IRRender::TextSize::SMALL, field);
}

inline IRAsset::BinaryStatus
readTextSize(IRAsset::BinaryReader &r, const char *field, IRRender::TextSize &out) {
    return readEnumByte(r, IRRender::TextSize::SMALL, field, out);
}

// Version 2 of a text-bearing widget is its version-1 record followed by the
// text size. `SaveSerialize<C>::readV1` reads that record; it stays frozen at
// the version-1 layout because the migration below reads old rows through it.
template <typename C>
IRAsset::Result<C> readWithTextSize(IRAsset::BinaryReader &r, const char *field) {
    using Res = IRAsset::Result<C>;
    Res value = SaveSerialize<C>::readV1(r);
    if (!value.ok()) {
        return value;
    }
    IR_SAVE_READ_STATUS(readTextSize(r, field, value.value_.textSize_));
    return value;
}

template <typename C> struct TextSizedWidgetMigration {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<C>>> migrators() {
        return {{1u, SaveSerialize<C>::readV1}};
    }
};

} // namespace detail

template <> struct SaveMigration<IRComponents::C_GizmoHandle> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_GizmoHandle>>>
    migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_GizmoHandle> {
                 detail::GizmoHandleV1 old{};
                 IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
                 if (!status.ok()) {
                     return IRAsset::Result<IRComponents::C_GizmoHandle>::error(
                         status.code_,
                         std::move(status.message_)
                     );
                 }
                 IRComponents::C_GizmoHandle handle{
                     old.kind_,
                     old.axis_,
                     old.referenceParams_,
                     old.referenceLocalPos_,
                     old.isAnchor_,
                     old.baseColor_,
                     old.anchorEntity_,
                 };
                 handle.hover_ = old.hover_;
                 return IRAsset::Result<IRComponents::C_GizmoHandle>::success(handle);
             }},
        };
    }
};

template <> struct SaveMigration<IRComponents::C_Camera> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_Camera>>>
    migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_Camera> {
                 // Consumed and discarded: read as the policy flag, the byte
                 // would turn the continuous zoom on from uninitialized memory.
                 detail::CameraV1 old{};
                 IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
                 if (!status.ok()) {
                     return IRAsset::Result<IRComponents::C_Camera>::error(
                         status.code_,
                         std::move(status.message_)
                     );
                 }
                 return IRAsset::Result<IRComponents::C_Camera>::success(IRComponents::C_Camera{});
             }},
        };
    }
};

template <> struct SaveSerialize<IRComponents::C_EntityCanvas> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_EntityCanvas &value) {
        w.writeU64(static_cast<std::uint64_t>(value.canvasEntity_));
        IRMath::BinaryIO::writeIVec2(w, value.canvasSize_);
        w.writeU8(value.visible_ ? 1 : 0);
        w.writeU8(value.screenLocked_ ? 1 : 0);
        w.writeI32(value.depthPriority_);
    }

    static IRAsset::Result<IRComponents::C_EntityCanvas> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_EntityCanvas>;
        std::uint64_t canvasEntity = 0;
        IRMath::ivec2 canvasSize{};
        bool visible = true;
        bool screenLocked = false;
        std::int32_t depthPriority = 0;

        IR_SAVE_READ(canvasEntity, r.readU64());
        IR_SAVE_READ(canvasSize, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ_BOOL(visible, r.readU8());
        IR_SAVE_READ_BOOL(screenLocked, r.readU8());
        IR_SAVE_READ(depthPriority, r.readI32());
        return Res::success(
            IRComponents::C_EntityCanvas{
                static_cast<IREntity::EntityId>(canvasEntity),
                canvasSize,
                visible,
                screenLocked,
                depthPriority,
            }
        );
    }
};

template <> struct SaveMigration<IRComponents::C_EntityCanvas> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_EntityCanvas>>>
    migrators() {
        return {
            {1u, detail::readEntityCanvasLegacy<detail::EntityCanvasV1>},
            {2u, detail::readEntityCanvasLegacy<detail::EntityCanvasV2>},
        };
    }
};

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

template <> struct SaveSerialize<IRComponents::C_TextStyle> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_TextStyle &value) {
        w.writeU32(value.color_.toPackedRGBA());
        w.writeI32(value.wrapWidth_);
        detail::writeEnumByte(
            w,
            value.alignH_,
            IRComponents::TextAlignH::RIGHT,
            "C_TextStyle horizontal alignment"
        );
        detail::writeEnumByte(
            w,
            value.alignV_,
            IRComponents::TextAlignV::BOTTOM,
            "C_TextStyle vertical alignment"
        );
        w.writeI32(value.boxWidth_);
        w.writeI32(value.boxHeight_);
        w.writeI32(value.fontSize_);
        detail::writeTextSize(w, value.textSize_, "C_TextStyle text size");
        IR_ASSERT(
            value.voxel_.voxelSize_ >= 0 && value.voxel_.voxelSize_ <= IRRender::kMaxVoxelTextSize,
            "C_TextStyle voxel size: saving {}, which read will reject",
            value.voxel_.voxelSize_
        );
        IR_ASSERT(
            value.voxel_.depth_ >= 1 && value.voxel_.depth_ <= IRRender::kMaxVoxelTextDepth,
            "C_TextStyle voxel depth: saving {}, which read will reject",
            value.voxel_.depth_
        );
        w.writeI32(value.voxel_.voxelSize_);
        w.writeI32(value.voxel_.depth_);
        detail::writeEnumByte(
            w,
            value.voxel_.facing_,
            IRRender::VoxelTextFacing::Y_FACE,
            "C_TextStyle voxel facing"
        );
        w.writeU32(value.faceColors_.x_.toPackedRGBA());
        w.writeU32(value.faceColors_.y_.toPackedRGBA());
        w.writeU32(value.faceColors_.z_.toPackedRGBA());
    }

    static IRAsset::Result<IRComponents::C_TextStyle> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_TextStyle>;
        IRAsset::Result<IRComponents::C_TextStyle> flat = readFlatFields(r);
        if (!flat.ok()) {
            return flat;
        }
        IRComponents::C_TextStyle value = flat.value_;
        IR_SAVE_READ(value.voxel_.voxelSize_, r.readI32());
        IR_SAVE_READ(value.voxel_.depth_, r.readI32());
        if (value.voxel_.voxelSize_ < 0 || value.voxel_.voxelSize_ > IRRender::kMaxVoxelTextSize ||
            value.voxel_.depth_ < 1 || value.voxel_.depth_ > IRRender::kMaxVoxelTextDepth) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_TextStyle: voxel size " + std::to_string(value.voxel_.voxelSize_) + " / depth " +
                    std::to_string(value.voxel_.depth_) + " is out of range"
            );
        }
        IR_SAVE_READ_STATUS(
            detail::readEnumByte(
                r,
                IRRender::VoxelTextFacing::Y_FACE,
                "C_TextStyle voxel facing",
                value.voxel_.facing_
            )
        );
        std::uint32_t faceX = 0;
        std::uint32_t faceY = 0;
        std::uint32_t faceZ = 0;
        IR_SAVE_READ(faceX, r.readU32());
        IR_SAVE_READ(faceY, r.readU32());
        IR_SAVE_READ(faceZ, r.readU32());
        value.faceColors_ = IRRender::VoxelFaceColors{
            IRMath::Color::fromPackedRGBA(faceX),
            IRMath::Color::fromPackedRGBA(faceY),
            IRMath::Color::fromPackedRGBA(faceZ),
        };
        return Res::success(value);
    }

    // The version 2 layout: every field but the voxel style.
    static IRAsset::Result<IRComponents::C_TextStyle> readFlatFields(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_TextStyle>;
        IRComponents::C_TextStyle value{};
        std::uint32_t packed = 0;
        IR_SAVE_READ(packed, r.readU32());
        value.color_ = IRMath::Color::fromPackedRGBA(packed);
        IR_SAVE_READ(value.wrapWidth_, r.readI32());
        IR_SAVE_READ_STATUS(
            detail::readEnumByte(
                r,
                IRComponents::TextAlignH::RIGHT,
                "C_TextStyle horizontal alignment",
                value.alignH_
            )
        );
        IR_SAVE_READ_STATUS(
            detail::readEnumByte(
                r,
                IRComponents::TextAlignV::BOTTOM,
                "C_TextStyle vertical alignment",
                value.alignV_
            )
        );
        IR_SAVE_READ(value.boxWidth_, r.readI32());
        IR_SAVE_READ(value.boxHeight_, r.readI32());
        IR_SAVE_READ(value.fontSize_, r.readI32());
        IR_SAVE_READ_STATUS(detail::readTextSize(r, "C_TextStyle text size", value.textSize_));
        return Res::success(value);
    }
};

template <> struct SaveMigration<IRComponents::C_TextStyle> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_TextStyle>>>
    migrators() {
        return {
            {1u,
             [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_TextStyle> {
                 using Res = IRAsset::Result<IRComponents::C_TextStyle>;
                 detail::TextStyleV1 old{};
                 IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
                 if (!status.ok()) {
                     return Res::error(status.code_, std::move(status.message_));
                 }
                 // The current reader's alignment checks, so a migrated row
                 // saves back without tripping the writer's assert.
                 if (!detail::enumInRange(old.alignH_, IRComponents::TextAlignH::RIGHT) ||
                     !detail::enumInRange(old.alignV_, IRComponents::TextAlignV::BOTTOM)) {
                     return Res::error(
                         IRAsset::BinaryIOError::UnknownTag,
                         "C_TextStyle: version 1 alignment (" +
                             std::to_string(static_cast<int>(old.alignH_)) + ", " +
                             std::to_string(static_cast<int>(old.alignV_)) + ") is outside [0, 2]"
                     );
                 }
                 return Res::success(
                     IRComponents::C_TextStyle{
                         old.color_,
                         old.wrapWidth_,
                         old.alignH_,
                         old.alignV_,
                         old.boxWidth_,
                         old.boxHeight_,
                         old.fontSize_,
                     }
                 );
             }},
            {2u, &SaveSerialize<IRComponents::C_TextStyle>::readFlatFields},
        };
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
        // live. `read` remains the enforcing check — these compile out under
        // `IR_RELEASE`.
        //
        // Split into the same two checks as `read`, for the same reason: one
        // message covering both faults mis-names whichever it wasn't written
        // for. A failing assert throws, so the sign check short-circuits the
        // cardinality check exactly as `read`'s early return does.
        IR_ASSERT(
            value.size_.x >= 0 && value.size_.y >= 0,
            "C_TrianglesOnlySet: saving a negative extent {}x{}; it is not indexable and read "
            "will reject this file",
            value.size_.x,
            value.size_.y
        );
        IR_ASSERT(
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
// Widget family. Every widget entity carries C_Widget + C_WidgetState plus one
// per-kind data component. Widget state has a hand-written empty-on-load
// format because all of its fields are transient. Of the per-kind components,
// C_WidgetColorSwatch and C_WidgetScroll remain plain data on the fast path.
// ---------------------------------------------------------------------------

template <> struct SaveSerialize<IRComponents::C_WidgetState> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetState &) {
        w.writeU8(0);
    }

    static IRAsset::Result<IRComponents::C_WidgetState> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetState>;
        std::uint8_t reserved = 0;
        IR_SAVE_READ(reserved, r.readU8());
        if (reserved != 0) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetState reserved format byte must be zero"
            );
        }
        return Res::success(IRComponents::C_WidgetState{});
    }
};

template <> struct SaveMigration<IRComponents::C_WidgetState> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_WidgetState>>>
    migrators() {
        return {{
            1u,
            [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_WidgetState> {
                detail::WidgetStateV1 old{};
                IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
                if (!status.ok()) {
                    return IRAsset::Result<IRComponents::C_WidgetState>::error(
                        status.code_,
                        std::move(status.message_)
                    );
                }
                return IRAsset::Result<IRComponents::C_WidgetState>::success(
                    IRComponents::C_WidgetState{}
                );
            },
        }};
    }
};

template <> struct SaveSerialize<IRComponents::C_Widget> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Widget &value) {
        detail::writeEnumByte(w, value.kind_, IRComponents::WidgetKind::IMAGE, "C_Widget kind");
        IRMath::BinaryIO::writeIVec2(w, value.size_);
        w.writeU8(value.disabled_ ? 1 : 0);
        w.writeU8(value.coversBelow_ ? 1 : 0);
        w.writeI32(value.zOrder_);
    }

    static IRAsset::Result<IRComponents::C_Widget> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Widget>;
        IRComponents::C_Widget value{};
        IR_SAVE_READ_STATUS(
            detail::readEnumByte(r, IRComponents::WidgetKind::IMAGE, "C_Widget kind", value.kind_)
        );
        IR_SAVE_READ(value.size_, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ_BOOL(value.disabled_, r.readU8());
        IR_SAVE_READ_BOOL(value.coversBelow_, r.readU8());
        IR_SAVE_READ(value.zOrder_, r.readI32());
        return Res::success(value);
    }
};

// A version-1 row predates the cover flag; it takes the value its builder
// sets, so a loaded panel or dropdown audits like a freshly built one.
template <> struct SaveMigration<IRComponents::C_Widget> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_Widget>>>
    migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_Widget> {
                 using Res = IRAsset::Result<IRComponents::C_Widget>;
                 detail::WidgetV1 old{};
                 IRAsset::BinaryStatus status = reader.readBytes(&old, sizeof(old));
                 if (!status.ok()) {
                     return Res::error(status.code_, std::move(status.message_));
                 }
                 // The current reader's kind check, so a migrated row saves
                 // back without tripping the writer's assert.
                 if (!detail::enumInRange(old.kind_, IRComponents::WidgetKind::IMAGE)) {
                     return Res::error(
                         IRAsset::BinaryIOError::UnknownTag,
                         "C_Widget: version 1 kind " + std::to_string(static_cast<int>(old.kind_)) +
                             " is outside [0, " +
                             std::to_string(static_cast<int>(IRComponents::WidgetKind::IMAGE)) + "]"
                     );
                 }
                 IRComponents::C_Widget value{};
                 value.kind_ = old.kind_;
                 value.size_ = old.size_;
                 value.disabled_ = old.disabled_;
                 value.coversBelow_ = old.kind_ == IRComponents::WidgetKind::PANEL ||
                                      old.kind_ == IRComponents::WidgetKind::DROPDOWN;
                 value.zOrder_ = old.zOrder_;
                 return Res::success(value);
             }},
        };
    }
};

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
        detail::writeTextSize(w, value.textSize_, "C_WidgetLabel text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetLabel> read(IRAsset::BinaryReader &r) {
        return detail::readWithTextSize<IRComponents::C_WidgetLabel>(r, "C_WidgetLabel text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetLabel> readV1(IRAsset::BinaryReader &r) {
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
        detail::writeTextSize(w, value.textSize_, "C_WidgetButton text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetButton> read(IRAsset::BinaryReader &r) {
        return detail::readWithTextSize<IRComponents::C_WidgetButton>(
            r,
            "C_WidgetButton text size"
        );
    }

    static IRAsset::Result<IRComponents::C_WidgetButton> readV1(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetButton>;
        IRComponents::C_WidgetButton value{};
        IR_SAVE_READ(value.label_, r.readString());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetSlider> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetSlider &value) {
        IR_ASSERT(IRMath::isFinite(value.minValue_), "C_WidgetSlider min must be finite");
        IR_ASSERT(IRMath::isFinite(value.maxValue_), "C_WidgetSlider max must be finite");
        IR_ASSERT(value.minValue_ < value.maxValue_, "C_WidgetSlider range must be ordered");
        IR_ASSERT(
            IRMath::isFinite(value.currentValue_) && value.currentValue_ >= value.minValue_ &&
                value.currentValue_ <= value.maxValue_,
            "C_WidgetSlider current value must be finite and inside its range"
        );
        IR_ASSERT(
            IRMath::isFinite(value.defaultValue_) && value.defaultValue_ >= value.minValue_ &&
                value.defaultValue_ <= value.maxValue_,
            "C_WidgetSlider default value must be finite and inside its range"
        );
        IR_ASSERT(
            IRMath::isFinite(value.step_) && value.step_ > 0.0f,
            "C_WidgetSlider step must be finite and above zero"
        );
        w.writeString(value.label_);
        w.writeF32(value.minValue_);
        w.writeF32(value.maxValue_);
        w.writeF32(value.currentValue_);
        w.writeF32(value.defaultValue_);
        w.writeF32(value.step_);
        w.writeU64(static_cast<std::uint64_t>(value.fieldId_));
        detail::writeTextSize(w, value.textSize_, "C_WidgetSlider text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetSlider> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetSlider>;
        IRComponents::C_WidgetSlider value{};
        IR_SAVE_READ(value.label_, r.readString());
        IR_SAVE_READ(value.minValue_, r.readF32());
        IR_SAVE_READ(value.maxValue_, r.readF32());
        IR_SAVE_READ(value.currentValue_, r.readF32());
        IR_SAVE_READ(value.defaultValue_, r.readF32());
        IR_SAVE_READ(value.step_, r.readF32());
        std::uint64_t field = 0;
        IR_SAVE_READ(field, r.readU64());
        value.fieldId_ = static_cast<IREntity::EntityId>(field);
        IR_SAVE_READ_STATUS(detail::readTextSize(r, "C_WidgetSlider text size", value.textSize_));
        IR_SAVE_READ_STATUS(validate(value));
        return Res::success(std::move(value));
    }

    static IRAsset::Result<IRComponents::C_WidgetSlider> readV1(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetSlider>;
        IRComponents::C_WidgetSlider value{};
        IR_SAVE_READ(value.label_, r.readString());
        IR_SAVE_READ(value.minValue_, r.readF32());
        IR_SAVE_READ(value.maxValue_, r.readF32());
        IR_SAVE_READ(value.currentValue_, r.readF32());
        deriveDefaults(value);
        IR_SAVE_READ_STATUS(validate(value));
        return Res::success(std::move(value));
    }

    static IRAsset::Result<IRComponents::C_WidgetSlider> readV2(IRAsset::BinaryReader &r) {
        return detail::readWithTextSize<IRComponents::C_WidgetSlider>(
            r,
            "C_WidgetSlider text size"
        );
    }

  private:
    static void deriveDefaults(IRComponents::C_WidgetSlider &value) {
        value.defaultValue_ = value.currentValue_;
        value.step_ = (value.maxValue_ - value.minValue_) / 100.0f;
        value.fieldId_ = IREntity::kNullEntity;
    }

    static IRAsset::BinaryStatus validate(const IRComponents::C_WidgetSlider &value) {
        if (!IRMath::isFinite(value.minValue_) || !IRMath::isFinite(value.maxValue_) ||
            !(value.minValue_ < value.maxValue_)) {
            return IRAsset::BinaryStatus::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetSlider range must be finite and ordered"
            );
        }
        if (!IRMath::isFinite(value.currentValue_) || value.currentValue_ < value.minValue_ ||
            value.currentValue_ > value.maxValue_) {
            return IRAsset::BinaryStatus::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetSlider current value must be finite and inside its range"
            );
        }
        if (!IRMath::isFinite(value.defaultValue_) || value.defaultValue_ < value.minValue_ ||
            value.defaultValue_ > value.maxValue_) {
            return IRAsset::BinaryStatus::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetSlider default value must be finite and inside its range"
            );
        }
        if (!IRMath::isFinite(value.step_) || !(value.step_ > 0.0f)) {
            return IRAsset::BinaryStatus::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetSlider step must be finite and above zero"
            );
        }
        return IRAsset::BinaryStatus::success();
    }
};

template <> struct SaveSerialize<IRComponents::C_WidgetCheckbox> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetCheckbox &value) {
        w.writeString(value.label_);
        w.writeU8(value.checked_ ? 1 : 0);
        detail::writeTextSize(w, value.textSize_, "C_WidgetCheckbox text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetCheckbox> read(IRAsset::BinaryReader &r) {
        return detail::readWithTextSize<IRComponents::C_WidgetCheckbox>(
            r,
            "C_WidgetCheckbox text size"
        );
    }

    static IRAsset::Result<IRComponents::C_WidgetCheckbox> readV1(IRAsset::BinaryReader &r) {
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
        detail::writeTextSize(w, value.textSize_, "C_WidgetDropdown text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetDropdown> read(IRAsset::BinaryReader &r) {
        return detail::readWithTextSize<IRComponents::C_WidgetDropdown>(
            r,
            "C_WidgetDropdown text size"
        );
    }

    static IRAsset::Result<IRComponents::C_WidgetDropdown> readV1(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetDropdown>;
        IRComponents::C_WidgetDropdown value{};
        IR_SAVE_READ_STATUS(detail::readStringVector(r, value.items_));
        IR_SAVE_READ(value.selectedIndex_, r.readI32());
        IR_SAVE_READ_BOOL(value.isOpen_, r.readU8());
        IR_SAVE_READ(value.itemHeight_, r.readI32());
        return Res::success(std::move(value));
    }
};

template <>
struct SaveMigration<IRComponents::C_WidgetLabel>
    : detail::TextSizedWidgetMigration<IRComponents::C_WidgetLabel> {};
template <>
struct SaveMigration<IRComponents::C_WidgetButton>
    : detail::TextSizedWidgetMigration<IRComponents::C_WidgetButton> {};
template <> struct SaveMigration<IRComponents::C_WidgetSlider> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_WidgetSlider>>>
    migrators() {
        return {
            {1u, SaveSerialize<IRComponents::C_WidgetSlider>::readV1},
            {2u, SaveSerialize<IRComponents::C_WidgetSlider>::readV2},
        };
    }
};
template <>
struct SaveMigration<IRComponents::C_WidgetCheckbox>
    : detail::TextSizedWidgetMigration<IRComponents::C_WidgetCheckbox> {};
template <>
struct SaveMigration<IRComponents::C_WidgetDropdown>
    : detail::TextSizedWidgetMigration<IRComponents::C_WidgetDropdown> {};

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

template <> struct SaveSerialize<IRComponents::C_WidgetNumberField> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetNumberField &value) {
        IR_ASSERT(IRMath::isFinite(value.value_), "C_WidgetNumberField value must be finite");
        IR_ASSERT(IRMath::isFinite(value.minValue_), "C_WidgetNumberField min must be finite");
        IR_ASSERT(IRMath::isFinite(value.maxValue_), "C_WidgetNumberField max must be finite");
        IR_ASSERT(IRMath::isFinite(value.step_), "C_WidgetNumberField step must be finite");
        IR_ASSERT(
            IRMath::isFinite(value.defaultValue_),
            "C_WidgetNumberField default must be finite"
        );
        IR_ASSERT(value.minValue_ < value.maxValue_, "C_WidgetNumberField range must be ordered");
        IR_ASSERT(value.step_ >= 0.0f, "C_WidgetNumberField step must not be negative");
        IR_ASSERT(
            value.value_ >= value.minValue_ && value.value_ <= value.maxValue_,
            "C_WidgetNumberField value must be inside its range"
        );
        IR_ASSERT(
            value.defaultValue_ >= value.minValue_ && value.defaultValue_ <= value.maxValue_,
            "C_WidgetNumberField default must be inside its range"
        );
        w.writeF32(value.value_);
        w.writeF32(value.minValue_);
        w.writeF32(value.maxValue_);
        w.writeF32(value.step_);
        w.writeF32(value.defaultValue_);
        detail::writeTextSize(w, value.textSize_, "C_WidgetNumberField text size");
    }

    static IRAsset::Result<IRComponents::C_WidgetNumberField> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetNumberField>;
        IRComponents::C_WidgetNumberField value{};
        IR_SAVE_READ(value.value_, r.readF32());
        IR_SAVE_READ(value.minValue_, r.readF32());
        IR_SAVE_READ(value.maxValue_, r.readF32());
        IR_SAVE_READ(value.step_, r.readF32());
        IR_SAVE_READ(value.defaultValue_, r.readF32());
        IR_SAVE_READ_STATUS(
            detail::readTextSize(r, "C_WidgetNumberField text size", value.textSize_)
        );
        if (!IRMath::isFinite(value.minValue_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField min must be finite"
            );
        }
        if (!IRMath::isFinite(value.maxValue_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField max must be finite"
            );
        }
        if (!(value.minValue_ < value.maxValue_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField min must be below max"
            );
        }
        if (!IRMath::isFinite(value.step_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField step must be finite"
            );
        }
        if (value.step_ < 0.0f) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField step must be non-negative"
            );
        }
        if (!IRMath::isFinite(value.value_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField value must be finite"
            );
        }
        if (value.value_ < value.minValue_ || value.value_ > value.maxValue_) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField value must be inside its range"
            );
        }
        if (!IRMath::isFinite(value.defaultValue_)) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField default must be finite"
            );
        }
        if (value.defaultValue_ < value.minValue_ || value.defaultValue_ > value.maxValue_) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField default must be inside its range"
            );
        }
        value.editBuffer_.clear();
        value.editing_ = false;
        return Res::success(std::move(value));
    }

    static IRAsset::Result<IRComponents::C_WidgetNumberField> readV1(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetNumberField>;
        IRComponents::C_WidgetNumberField value{};
        IR_SAVE_READ(value.value_, r.readF32());
        IR_SAVE_READ(value.minValue_, r.readF32());
        IR_SAVE_READ(value.maxValue_, r.readF32());
        IR_SAVE_READ(value.step_, r.readF32());
        value.defaultValue_ = value.value_;
        IR_SAVE_READ_STATUS(
            detail::readTextSize(r, "C_WidgetNumberField text size", value.textSize_)
        );
        if (!IRMath::isFinite(value.minValue_) || !IRMath::isFinite(value.maxValue_) ||
            !(value.minValue_ < value.maxValue_) || !IRMath::isFinite(value.step_) ||
            value.step_ < 0.0f || !IRMath::isFinite(value.value_) ||
            value.value_ < value.minValue_ || value.value_ > value.maxValue_) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetNumberField version 1 values are invalid"
            );
        }
        value.editBuffer_.clear();
        value.editing_ = false;
        return Res::success(std::move(value));
    }
};

template <> struct SaveMigration<IRComponents::C_WidgetNumberField> {
    static std::vector<
        std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_WidgetNumberField>>>
    migrators() {
        return {{1u, SaveSerialize<IRComponents::C_WidgetNumberField>::readV1}};
    }
};

// The texels are saved, not re-decoded on load: the snapshot restores what
// was drawn even when the PNG has since moved or changed.
template <> struct SaveSerialize<IRComponents::C_WidgetImage> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_WidgetImage &value) {
        IR_ASSERT(
            value.texelSize_.x >= 0 && value.texelSize_.y >= 0,
            "C_WidgetImage: saving a negative texel extent {}x{}; read will reject this file",
            value.texelSize_.x,
            value.texelSize_.y
        );
        IR_ASSERT(
            static_cast<std::int64_t>(value.texels_.size()) ==
                static_cast<std::int64_t>(value.texelSize_.x) *
                    static_cast<std::int64_t>(value.texelSize_.y),
            "C_WidgetImage: saving {} texels for the extent {}x{}; read will reject this file",
            value.texels_.size(),
            value.texelSize_.x,
            value.texelSize_.y
        );
        w.writeString(value.path_);
        IRMath::BinaryIO::writeIVec2(w, value.texelSize_);
        detail::writeTrivialVector(w, value.texels_);
        w.writeU8(value.loaded_ ? 1 : 0);
        w.writeString(value.error_);
    }

    static IRAsset::Result<IRComponents::C_WidgetImage> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_WidgetImage>;
        IRComponents::C_WidgetImage value{};
        IR_SAVE_READ(value.path_, r.readString());
        IR_SAVE_READ(value.texelSize_, IRMath::BinaryIO::readIVec2(r));
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.texels_));
        IR_SAVE_READ_BOOL(value.loaded_, r.readU8());
        IR_SAVE_READ(value.error_, r.readString());

        // WIDGET_RENDER_IMAGE copies rows of `texelSize_.x` texels out of
        // `texels_`, so a negative extent or a short array reads out of bounds.
        if (value.texelSize_.x < 0 || value.texelSize_.y < 0) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetImage: negative texel extent " + std::to_string(value.texelSize_.x) + "x" +
                    std::to_string(value.texelSize_.y) + " is not indexable"
            );
        }
        const std::int64_t expectedTexels = static_cast<std::int64_t>(value.texelSize_.x) *
                                            static_cast<std::int64_t>(value.texelSize_.y);
        if (static_cast<std::int64_t>(value.texels_.size()) != expectedTexels) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WidgetImage: " + std::to_string(value.texels_.size()) +
                    " texels disagree with the extent " + std::to_string(value.texelSize_.x) + "x" +
                    std::to_string(value.texelSize_.y)
            );
        }
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_RENDER_H */
