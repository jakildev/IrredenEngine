#ifndef IR_PREFAB_CANVAS_DRAW_H
#define IR_PREFAB_CANVAS_DRAW_H

// Immediate-mode drawing onto any named trixel canvas at a chosen composite
// depth: flat text, isometric voxel text, filled rectangles, borders and
// decoded images. A call queues glyph commands or an image draw into the
// `C_CanvasDrawQueue` singleton; once per frame `System<CANVAS_DRAW>`
// rasterizes every batch through the text compute path that `TEXT_TO_TRIXEL`
// owns and its own image kernel, so a draw costs no CPU texture upload and
// must be re-issued every frame, like `IRGui.fillRect`. An image is decoded
// and uploaded once by `loadImage`; drawing it samples that texture.
//
// Where a draw lands on screen is decided by the depth it writes, because the
// framebuffer composite depth-tests every canvas texel (GL_LESS on the
// distance texture) rather than layering canvases in draw order:
//
//   kDepthBehind     in front of the background canvas's clear, behind every
//                    world voxel;
//   kDepthWorldNear .. kDepthWorldFar   inside the world's band, where scene
//                    geometry can occlude it;
//   kDepthFront      the far edge of the reserved foreground band: in front of
//                    all world content, behind foreground-priority entity
//                    canvases.
//
// A draw onto a canvas composited into the main framebuffer is glitched with
// the world by `FRAMEBUFFER_GLITCH`; a draw onto the "gui" canvas (composited
// on screen after the pass) stays clean. `createLayer` makes a canvas sized
// like the main one, parented to the main framebuffer, that nothing else
// draws into — the usual target for an overlay behind or in front of the
// scene. Adoption: `docs/design/canvas-draw-layers.md`.

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_persistent.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/components/component_canvas_draw_images.hpp>
#include <irreden/render/components/component_canvas_draw_layer.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/entities/entity_trixel_canvas.hpp>
#include <irreden/render/async_texture.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/voxel_text.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRPrefab::CanvasDraw {

constexpr int kDepthBehind = IRConstants::kTrixelDistanceMaxDistance - 1;
constexpr int kDepthWorldFar = IRConstants::kTrixelDistanceMaxDistance - 2;
constexpr int kDepthWorldNear = IRRender::kDepthForegroundCeil + 1;
constexpr int kDepthFront = IRRender::kDepthForegroundCeil;

static_assert(
    kDepthFront < kDepthWorldNear && kDepthWorldNear < kDepthWorldFar &&
        kDepthWorldFar < kDepthBehind,
    "front, world and behind depths must sit nearer to farther, since the composite is GL_LESS"
);

struct TextOptions {
    IRMath::Color color_ = IRMath::IRColors::kWhite;
    int fontSize_ = 1;
    IRRender::TextSize size_ = IRRender::TextSize::LARGE;
    IRComponents::TextAlignH alignH_ = IRComponents::TextAlignH::LEFT;
    IRComponents::TextAlignV alignV_ = IRComponents::TextAlignV::TOP;
    // 0 = the canvas extent, as C_TextStyle's box fields.
    int boxWidth_ = 0;
    int boxHeight_ = 0;
    // 0 = no wrap, -1 = the canvas edge, else a width in texels.
    int wrapWidth_ = 0;
};

struct VoxelTextOptions {
    IRRender::VoxelFaceColors faces_{};
    IRRender::VoxelTextStyle style_{1, 1, IRRender::VoxelTextFacing::CAMERA};
    IRComponents::TextAlignH alignH_ = IRComponents::TextAlignH::LEFT;
    IRComponents::TextAlignV alignV_ = IRComponents::TextAlignV::TOP;
    int boxWidth_ = 0;
    int boxHeight_ = 0;
};

// A run of `count_` cubes along `axis_`, the first `lit_` in `lit_faces_`
// and the rest in `unlit_faces_` (a segmented meter).
struct VoxelRunOptions {
    int count_ = 1;
    int lit_ = 0;
    int voxelSize_ = 1;
    IRRender::VoxelRunAxis axis_ = IRRender::VoxelRunAxis::X;
    IRRender::VoxelFaceColors lit_faces_{};
    IRRender::VoxelFaceColors unlit_faces_{};
};

// A loaded image, from `loadImage`. `kInvalidImage` is no image.
using ImageHandle = std::uint32_t;
constexpr ImageHandle kInvalidImage = 0;
// The longer side an image keeps on the GPU; a larger file is box-filtered
// down to it once, at load.
constexpr int kMaxImageDimension = 4096;
constexpr int kMaxImageCell = 4096;
constexpr int kMaxImagePosterizeLevels = 64;

struct ImageOptions {
    // The source crop mapped onto the drawn box, (u0, v0, u1, v1) in 0..1
    // with (0, 0) the image's top-left. u1 < u0 or v1 < v0 mirrors.
    IRMath::vec4 uvRect_ = IRMath::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    // Mosaic: every cell_ x cell_ block of canvas texels, counted from the
    // box's top-left, is the one averaged colour of the source under it.
    int cell_ = 1;
    IRMath::Color tint_ = IRMath::IRColors::kWhite;
    // 0 = off; otherwise each channel snaps to this many levels (2 ..
    // kMaxImagePosterizeLevels) before the tint multiplies it.
    int levels_ = 0;
};

// What a loader hands `loadImage` for one decoded file: the file's pixel
// size and the `MipAtlas` texture built from it.
struct ImageSource {
    IRMath::ivec2 sourceSize_ = IRMath::ivec2(0);
    IRMath::ivec2 baseSize_ = IRMath::ivec2(0);
    int levelCount_ = 0;
    std::pair<IRRender::ResourceId, IRRender::Texture2D *> texture_{0, nullptr};
};
// Fills the source for `path`, or returns false with the reason in `error`.
using ImageSourceLoader =
    std::function<bool(const std::string &path, ImageSource &source, std::string &error)>;

inline IRComponents::C_CanvasDrawQueue &queue() {
    return IREntity::singleton<IRComponents::C_CanvasDrawQueue>();
}

inline IRComponents::C_CanvasDrawImages &images() {
    return IREntity::singleton<IRComponents::C_CanvasDrawImages>();
}

// Registration-time singleton touch, so the lazy create never first runs
// inside a tick (`widget_theme.hpp` documents why).
inline void ensureQueueSingleton() {
    queue();
}

inline void ensureImagesSingleton() {
    images();
}

namespace detail {

// The batch collecting `canvas`'s draws this frame, opened on first use. An
// empty batch re-reads the canvas size, since a canvas can resize between
// frames (`RenderManager::resizeGuiCanvas`).
inline IRComponents::C_CanvasDrawQueue::Batch &
batchFor(IRComponents::C_CanvasDrawQueue &queue, IREntity::EntityId canvas) {
    for (IRComponents::C_CanvasDrawQueue::Batch &batch : queue.batches_) {
        if (batch.canvas_ == canvas) {
            if (batch.primitives_.empty()) {
                batch.canvasSize_ =
                    IREntity::getComponent<IRComponents::C_SizeTriangles>(canvas).size_;
            }
            return batch;
        }
    }
    queue.batches_.push_back(
        {canvas, IREntity::getComponent<IRComponents::C_SizeTriangles>(canvas).size_}
    );
    return queue.batches_.back();
}

// The texel box a command may write: its glyph cell scaled by its font
// size, widened by one scale on the right and below and one above, the
// apron the lattice display's triangles reach past the cell.
inline void
commandBounds(const IRRender::GlyphDrawCommand &cmd, IRMath::ivec2 &lo, IRMath::ivec2 &hi) {
    const int scale = IRMath::max(static_cast<int>(cmd.styleFlags & 0xFFu), 1);
    const int cellWidth = static_cast<int>(
        (cmd.styleFlags >> IRRender::kGlyphStyleCellWidthShift) & IRRender::kGlyphStyleCellMask
    );
    const int cellHeight = static_cast<int>(
        (cmd.styleFlags >> IRRender::kGlyphStyleCellHeightShift) & IRRender::kGlyphStyleCellMask
    );
    const IRMath::ivec2 pos = IRRender::unpackGlyphPosition(cmd.positionPacked);
    lo = IRMath::ivec2(pos.x, pos.y - scale);
    hi = pos + IRMath::ivec2((cellWidth + 1) * scale, (cellHeight + 1) * scale);
}

// Stamps `distance` on the commands appended to `batch` since `first` (the
// text expanders stamp the GUI text distance; a canvas draw carries its own)
// and records them as one primitive; nothing when none was appended (an
// empty or fully clipped draw).
inline void
closePrimitive(IRComponents::C_CanvasDrawQueue::Batch &batch, std::size_t first, int distance) {
    if (batch.commands_.size() <= first) {
        return;
    }
    IRMath::ivec2 lo;
    IRMath::ivec2 hi;
    commandBounds(batch.commands_[first], lo, hi);
    for (std::size_t i = first; i < batch.commands_.size(); ++i) {
        IRRender::GlyphDrawCommand &cmd = batch.commands_[i];
        cmd.distance = static_cast<std::uint32_t>(distance);
        IRMath::ivec2 cmdLo;
        IRMath::ivec2 cmdHi;
        commandBounds(cmd, cmdLo, cmdHi);
        lo.x = IRMath::min(lo.x, cmdLo.x);
        lo.y = IRMath::min(lo.y, cmdLo.y);
        hi.x = IRMath::max(hi.x, cmdHi.x);
        hi.y = IRMath::max(hi.y, cmdHi.y);
    }
    batch.primitives_.push_back({lo, hi, batch.commands_.size()});
}

inline IRComponents::C_CanvasDrawImages::Image *
findImage(IRComponents::C_CanvasDrawImages &registry, ImageHandle handle) {
    for (IRComponents::C_CanvasDrawImages::Image &image : registry.images_) {
        if (image.handle_ == handle) {
            return &image;
        }
    }
    return nullptr;
}

// Decodes `path`, builds its mip atlas and uploads it.
inline bool uploadImageFile(const std::string &path, ImageSource &source, std::string &error) {
    const IRRender::ImageData decoded{path.c_str(), error};
    if (decoded.data_ == nullptr) {
        return false;
    }
    source.sourceSize_ = IRMath::ivec2(decoded.width_, decoded.height_);
    IRRender::MipAtlas atlas =
        IRRender::buildMipAtlas(decoded.data_, source.sourceSize_, kMaxImageDimension);
    source.baseSize_ = atlas.baseSize_;
    source.levelCount_ = atlas.levelCount_;
    const IRRender::DecodedImage pixels{
        std::move(atlas.pixels_),
        atlas.atlasSize_.x,
        atlas.atlasSize_.y
    };
    source.texture_ = IRRender::uploadDecodedImage(pixels);
    return true;
}

// The kernel command drawing `image` into the `pos` / `size` box, whose part
// on the canvas is `[clipLo, clipHi)`. Out-of-range options are pulled into
// range: the crop into 0..1, the cell to 1 .. kMaxImageCell, and a posterize
// count below 2 to off.
inline IRRender::CanvasImageCommand imageCommand(
    const IRComponents::C_CanvasDrawImages::Image &image,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::ivec2 clipLo,
    IRMath::ivec2 clipHi,
    const ImageOptions &options,
    int distance
) {
    IRRender::CanvasImageCommand command;
    command.clipLo_ = clipLo;
    command.clipHi_ = clipHi;
    command.origin_ = pos;
    command.size_ = size;
    command.uvRect_ = IRMath::clamp(options.uvRect_, IRMath::vec4(0.0f), IRMath::vec4(1.0f));
    command.baseSize_ = image.baseSize_;
    command.levelCount_ = image.levelCount_;
    command.cell_ = IRMath::clamp(options.cell_, 1, kMaxImageCell);
    command.tintPacked_ = options.tint_.toPackedRGBA();
    command.posterizeLevels_ =
        options.levels_ < 2 ? 0 : IRMath::min(options.levels_, kMaxImagePosterizeLevels);
    command.distance_ = distance;
    return command;
}

inline bool primitivesMeet(
    const IRComponents::C_CanvasDrawQueue::Batch::Primitive &a,
    const IRComponents::C_CanvasDrawQueue::Batch::Primitive &b
) {
    return a.lo_.x < b.hi_.x && b.lo_.x < a.hi_.x && a.lo_.y < b.hi_.y && b.lo_.y < a.hi_.y;
}

} // namespace detail

// The end of the run of primitives from `begin` that may rasterize in any
// order: the index of the first later primitive whose box meets one in the
// run, or the count. A compute dispatch orders nothing, so a draw over an
// earlier one must dispatch after it to land on top; a run's glyph draws
// share one dispatch and each of its images takes its own. Quadratic in the
// run's length, cheap at the hundreds of primitives a frame's overlays queue.
inline std::size_t overlapGroupEnd(
    std::span<const IRComponents::C_CanvasDrawQueue::Batch::Primitive> primitives, std::size_t begin
) {
    for (std::size_t i = begin + 1; i < primitives.size(); ++i) {
        for (std::size_t j = begin; j < i; ++j) {
            if (detail::primitivesMeet(primitives[j], primitives[i])) {
                return i;
            }
        }
    }
    return primitives.size();
}

inline void queueText(
    IREntity::EntityId canvas,
    std::string_view text,
    IRMath::ivec2 pos,
    const TextOptions &options,
    int distance
) {
    if (text.empty()) {
        return;
    }
    IRComponents::C_CanvasDrawQueue::Batch &batch = detail::batchFor(queue(), canvas);
    const std::size_t first = batch.commands_.size();
    GuiText::expandTextToCommands(
        batch.commands_,
        text,
        pos,
        batch.canvasSize_,
        options.color_,
        options.wrapWidth_,
        options.alignH_,
        options.alignV_,
        options.boxWidth_,
        options.boxHeight_,
        options.fontSize_,
        options.size_
    );
    detail::closePrimitive(batch, first, distance);
}

inline void queueVoxelText(
    IREntity::EntityId canvas,
    std::string_view text,
    IRMath::ivec2 pos,
    const VoxelTextOptions &options,
    int distance
) {
    if (text.empty()) {
        return;
    }
    IRComponents::C_CanvasDrawQueue &drawQueue = queue();
    IRRender::layoutVoxelText(
        text,
        options.style_,
        drawQueue.voxelLayout_,
        drawQueue.voxelScratch_
    );
    IRComponents::C_CanvasDrawQueue::Batch &batch = detail::batchFor(drawQueue, canvas);
    const std::size_t first = batch.commands_.size();
    GuiText::expandVoxelTextToCommands(
        batch.commands_,
        drawQueue.voxelLayout_,
        pos,
        batch.canvasSize_,
        options.faces_,
        options.alignH_,
        options.alignV_,
        options.boxWidth_,
        options.boxHeight_
    );
    detail::closePrimitive(batch, first, distance);
}

// The texel extent `text` covers in `style`, before any alignment.
inline IRMath::ivec2
measureVoxelText(std::string_view text, const IRRender::VoxelTextStyle &style) {
    IRComponents::C_CanvasDrawQueue &drawQueue = queue();
    IRRender::layoutVoxelText(text, style, drawQueue.voxelLayout_, drawQueue.voxelScratch_);
    return drawQueue.voxelLayout_.size_;
}

// A run of cubes with its box's top-left at `pos` (voxelRunSize measures it;
// voxelRunCellRect places a cube inside it).
inline void queueVoxelRun(
    IREntity::EntityId canvas, IRMath::ivec2 pos, const VoxelRunOptions &options, int distance
) {
    IRComponents::C_CanvasDrawQueue &drawQueue = queue();
    IRRender::layoutVoxelRun(
        options.count_,
        options.lit_,
        options.voxelSize_,
        options.axis_,
        drawQueue.voxelLayout_,
        drawQueue.voxelScratch_
    );
    IRComponents::C_CanvasDrawQueue::Batch &batch = detail::batchFor(drawQueue, canvas);
    const std::size_t first = batch.commands_.size();
    GuiText::expandVoxelRunToCommands(
        batch.commands_,
        drawQueue.voxelLayout_,
        pos,
        batch.canvasSize_,
        options.lit_faces_,
        options.unlit_faces_
    );
    detail::closePrimitive(batch, first, distance);
}

// A filled rectangle, clipped to the canvas, tiled by solid glyph cells.
inline void queueRect(
    IREntity::EntityId canvas,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color,
    int distance
) {
    IRComponents::C_CanvasDrawQueue::Batch &batch = detail::batchFor(queue(), canvas);
    IRMath::ivec2 lo;
    IRMath::ivec2 hi;
    if (!IRRender::clipRectToCanvas(pos, size, batch.canvasSize_, lo, hi)) {
        return;
    }
    const std::size_t first = batch.commands_.size();
    const std::uint32_t packed = color.toPackedRGBA();
    for (int y = lo.y; y < hi.y; y += IRRender::kGlyphHeight) {
        const int h = IRMath::min(IRRender::kGlyphHeight, hi.y - y);
        for (int x = lo.x; x < hi.x; x += IRRender::kGlyphWidth) {
            const int w = IRMath::min(IRRender::kGlyphWidth, hi.x - x);
            batch.commands_.push_back(
                GuiText::solidCellCommand(
                    IRMath::ivec2(x, y),
                    IRMath::ivec2(w, h),
                    packed,
                    distance
                )
            );
        }
    }
    detail::closePrimitive(batch, first, distance);
}

// Decodes the image file at `path` (PNG, JPG, anything stb reads) and uploads
// it, returning its handle, or `kInvalidImage` with the reason in `error`.
// Loading a path that is already loaded returns the same handle and counts
// one more reference. Decode and upload are synchronous: call it at setup,
// not per frame. `loader` stands in for the decode and upload.
inline ImageHandle loadImage(
    const std::string &path,
    std::string &error,
    const ImageSourceLoader &loader = detail::uploadImageFile
) {
    IRComponents::C_CanvasDrawImages &registry = images();
    for (IRComponents::C_CanvasDrawImages::Image &image : registry.images_) {
        if (image.path_ == path) {
            ++image.references_;
            return image.handle_;
        }
    }
    ImageSource source;
    if (!loader(path, source, error)) {
        return kInvalidImage;
    }
    const ImageHandle handle = registry.nextHandle_++;
    registry.images_.push_back(
        {handle, 1, path, source.sourceSize_, source.baseSize_, source.levelCount_, source.texture_}
    );
    return handle;
}

inline ImageHandle loadImage(const std::string &path) {
    std::string error;
    const ImageHandle handle = loadImage(path, error);
    if (handle == kInvalidImage) {
        IRE_LOG_WARN("IRPrefab::CanvasDraw::loadImage: {}", error);
    }
    return handle;
}

inline bool isImage(ImageHandle handle) {
    return detail::findImage(images(), handle) != nullptr;
}

// The decoded file's size in pixels; zero for a handle that names no image.
inline IRMath::ivec2 imageSize(ImageHandle handle) {
    const IRComponents::C_CanvasDrawImages::Image *image = detail::findImage(images(), handle);
    return image != nullptr ? image->sourceSize_ : IRMath::ivec2(0);
}

// Drops one reference to the image; the last one frees its texture and
// retires the handle. Nothing for a handle that names no image.
inline void releaseImage(ImageHandle handle) {
    IRComponents::C_CanvasDrawImages &registry = images();
    for (std::size_t i = 0; i < registry.images_.size(); ++i) {
        IRComponents::C_CanvasDrawImages::Image &image = registry.images_[i];
        if (image.handle_ != handle) {
            continue;
        }
        if (--image.references_ > 0) {
            return;
        }
        if (image.texture_.second != nullptr) {
            IRRender::destroyResource<IRRender::Texture2D>(image.texture_.first);
        }
        registry.images_.erase(registry.images_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

// The image stretched over the `pos` / `size` box of canvas texels, clipped
// to the canvas. A canvas texel is two framebuffer pixels wide and one tall,
// so a box of `size` shows the source undistorted when
// 2 * size.x / size.y equals the crop's pixel aspect. Texels the source
// leaves transparent keep what the canvas held. Nothing for a handle that
// names no image.
inline void queueImage(
    IREntity::EntityId canvas,
    ImageHandle handle,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    const ImageOptions &options,
    int distance
) {
    const IRComponents::C_CanvasDrawImages::Image *image = detail::findImage(images(), handle);
    if (image == nullptr) {
        return;
    }
    IRComponents::C_CanvasDrawQueue::Batch &batch = detail::batchFor(queue(), canvas);
    IRMath::ivec2 lo;
    IRMath::ivec2 hi;
    if (!IRRender::clipRectToCanvas(pos, size, batch.canvasSize_, lo, hi)) {
        return;
    }
    batch.images_.push_back(
        {handle, detail::imageCommand(*image, pos, size, lo, hi, options, distance)}
    );
    batch.primitives_.push_back(
        {lo, hi, batch.commands_.size(), static_cast<int>(batch.images_.size()) - 1}
    );
}

// A hollow border `thickness` texels wide inside `pos` / `size`.
inline void queueBorder(
    IREntity::EntityId canvas,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color,
    int distance,
    int thickness
) {
    if (size.x <= 0 || size.y <= 0 || thickness <= 0) {
        return;
    }
    const int t = IRMath::min(thickness, IRMath::min(size.x, size.y));
    queueRect(canvas, pos, IRMath::ivec2(size.x, t), color, distance);
    queueRect(
        canvas,
        IRMath::ivec2(pos.x, pos.y + size.y - t),
        IRMath::ivec2(size.x, t),
        color,
        distance
    );
    queueRect(
        canvas,
        IRMath::ivec2(pos.x, pos.y + t),
        IRMath::ivec2(t, size.y - 2 * t),
        color,
        distance
    );
    queueRect(
        canvas,
        IRMath::ivec2(pos.x + size.x - t, pos.y + t),
        IRMath::ivec2(t, size.y - 2 * t),
        color,
        distance
    );
}

// A trixel canvas the size of the main canvas, composited into the main
// framebuffer at zoom 1 with the GUI canvas's lattice display, cleared each
// frame by `System<CANVAS_DRAW>` and registered under `name` for
// `IRRender::getCanvas`. Its texel space is the GUI canvas's at gui scale 1.
// `name` must not be a registered canvas yet.
inline IREntity::EntityId createLayer(const std::string &name) {
    IR_ASSERT(!IRRender::hasCanvas(name), "createLayer: a canvas of that name exists");
    const IRMath::ivec2 size =
        IREntity::getComponent<IRComponents::C_SizeTriangles>(IRRender::getCanvas("main")).size_;
    const IREntity::EntityId canvas =
        IREntity::createEntity<IREntity::PrefabTypes::kTrixelCanvas>(name, size);
    IREntity::setComponent(
        canvas,
        IRComponents::
            C_TrixelCanvasRenderBehavior{false, false, false, false, false, 0.0f, 0.0f, 0.0f, 0.0f}
    );
    IREntity::setComponent(canvas, IRComponents::C_CanvasDrawLayer{});
    IREntity::setComponent(canvas, IRComponents::C_Persistent{});
    IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(canvas).renderedSampleLayout_ =
        IRRender::TrixelSampleLayout::LOCAL_TRIANGLES;
    IRRender::registerCanvas(name, canvas);
    return canvas;
}

} // namespace IRPrefab::CanvasDraw

#endif /* IR_PREFAB_CANVAS_DRAW_H */
