#ifndef LUA_CANVAS_DRAW_BINDINGS_H
#define LUA_CANVAS_DRAW_BINDINGS_H

// Lua surface for `IRPrefab::CanvasDraw`, installed as the `IRCanvas` table by
// `LuaScript::bindLuaCanvasDraw()`. Every draw is queued for the frame and
// shown by `CANVAS_DRAW` (`framebufferImage` by `IMAGE_TO_FRAMEBUFFER`), so a
// script re-issues it each frame.
//
//   IRCanvas.createLayer(name)                 -- a main-sized layer canvas
//   local w, h = IRCanvas.size(name)
//   IRCanvas.text(name, text, x, y, { color, fontSize, size, distance,
//                                     alignH, alignV, boxWidth, boxHeight, wrapWidth })
//   IRCanvas.voxelText(name, text, x, y, { color | faceX, faceY, faceZ, voxelSize, depth,
//                                          facing, distance, alignH, alignV, boxWidth, boxHeight })
//   local w, h = IRCanvas.measureVoxelText(text, { voxelSize, depth, facing })
//   IRCanvas.fillRect(name, x, y, w, h, color, distance?)
//   IRCanvas.drawBorder(name, x, y, w, h, color, thickness?, distance?)
//   local handle, err = IRCanvas.loadImage(path)   -- image-base relative; nil, err on failure
//   local w, h = IRCanvas.imageSize(handle)        -- the file's pixel size
//   IRCanvas.releaseImage(handle)
//   IRCanvas.image(name, handle, x, y, w, h, { uv = { u0, v0, u1, v1 }, cell, tint, levels,
//                                              rotation, distance })
//   local w, h = IRCanvas.framebufferSize()        -- the main framebuffer, in pixels
//   IRCanvas.framebufferImage(handle, x, y, w, h, { uv, cell, tint, levels, rotation, distance })
//                                                  -- the box and cell in framebuffer pixels;
//                                                  -- rotation in radians, clockwise
//   IRCanvas.Depth.{BEHIND, WORLD_NEAR, WORLD_FAR, FRONT}  -- composite depths
//   IRCanvas.AlignH.{LEFT, CENTER, RIGHT}, IRCanvas.AlignV.{TOP, CENTER, BOTTOM}
//
// A canvas is named as `IRRender.getCanvas` knows it ("gui", "background",
// or a layer); `framebufferImage` names none, it draws into the main
// framebuffer at one sample per pixel. `distance` defaults to
// IRCanvas.Depth.FRONT. An unknown option key, an unknown canvas, an unknown
// image handle, or a value outside its range raises before anything is queued.
// `loadImage` reports a file it cannot decode, or a path outside the image
// base, through its second return instead.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/voxel_text.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_text_bindings.hpp>
#include <irreden/utility/path_utils.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace IRScript::detail {

using CanvasResolver = std::function<IREntity::EntityId(const std::string &)>;
using LayerFactory = std::function<IREntity::EntityId(const std::string &)>;
// Loads the image at an already contained path, or returns kInvalidImage with
// the reason in `error`.
using ImageLoader =
    std::function<IRPrefab::CanvasDraw::ImageHandle(const std::string &path, std::string &error)>;

inline IREntity::EntityId renderCanvasByName(const std::string &name) {
    return IRRender::hasCanvas(name) ? IRRender::getCanvas(name) : IREntity::kNullEntity;
}

inline IREntity::EntityId renderLayer(const std::string &name) {
    if (IRRender::hasCanvas(name)) {
        return IREntity::kNullEntity;
    }
    return IRPrefab::CanvasDraw::createLayer(name);
}

inline IRPrefab::CanvasDraw::ImageHandle renderImage(const std::string &path, std::string &error) {
    return IRPrefab::CanvasDraw::loadImage(path, error);
}

// The main framebuffer's size in pixels.
using FramebufferSizeResolver = std::function<IRMath::ivec2()>;

inline IRMath::ivec2 renderFramebufferSize() {
    return IRPrefab::CanvasDraw::framebufferSize();
}

// The handle a Lua value spells, or kInvalidImage when it is not a positive
// whole number.
inline IRPrefab::CanvasDraw::ImageHandle canvasDrawImageHandle(const sol::object &value) {
    if (!value.valid() || value.get_type() != sol::type::number) {
        return IRPrefab::CanvasDraw::kInvalidImage;
    }
    const double number = value.as<double>();
    if (!IRMath::isFinite(number) || number != IRMath::floor(number) || number < 1.0 ||
        number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return IRPrefab::CanvasDraw::kInvalidImage;
    }
    return static_cast<IRPrefab::CanvasDraw::ImageHandle>(number);
}

// The handle of a loaded image, raising for anything else.
inline IRPrefab::CanvasDraw::ImageHandle
requireCanvasDrawImage(const sol::object &value, const char *caller) {
    const IRPrefab::CanvasDraw::ImageHandle handle = canvasDrawImageHandle(value);
    if (!IRPrefab::CanvasDraw::isImage(handle)) {
        throw sol::error{
            std::string(caller) + ": handle must be a loaded image (IRCanvas.loadImage's result)"
        };
    }
    return handle;
}

// The `uv` crop: four numbers in [0, 1], the whole image when absent.
inline IRMath::vec4 canvasDrawUvRect(const sol::table &options, const char *caller) {
    IRMath::vec4 rect(0.0f, 0.0f, 1.0f, 1.0f);
    const sol::object value = options["uv"];
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return rect;
    }
    const std::string message = std::string(caller) + ": uv must be { u0, v0, u1, v1 } in [0, 1]";
    if (value.get_type() != sol::type::table) {
        throw sol::error{message};
    }
    const sol::table uv = value.as<sol::table>();
    if (uv.size() != 4) {
        throw sol::error{message};
    }
    for (int i = 0; i < 4; ++i) {
        const sol::object component = uv[i + 1];
        if (component.get_type() != sol::type::number) {
            throw sol::error{message};
        }
        const double number = component.as<double>();
        if (!IRMath::isFinite(number) || number < 0.0 || number > 1.0) {
            throw sol::error{message};
        }
        rect[i] = static_cast<float>(number);
    }
    return rect;
}

inline int canvasDrawDistance(const sol::table &options, const char *caller) {
    return integerOption(
        options,
        "distance",
        IRPrefab::CanvasDraw::kDepthFront,
        IRConstants::kTrixelDistanceMinDistance,
        IRConstants::kTrixelDistanceMaxDistance,
        caller
    );
}

inline IRComponents::TextAlignH canvasDrawAlignH(const sol::table &options, const char *caller) {
    switch (integerOption(options, "alignH", 0, 0, 2, caller)) {
    case 1:
        return IRComponents::TextAlignH::CENTER;
    case 2:
        return IRComponents::TextAlignH::RIGHT;
    default:
        return IRComponents::TextAlignH::LEFT;
    }
}

inline IRComponents::TextAlignV canvasDrawAlignV(const sol::table &options, const char *caller) {
    switch (integerOption(options, "alignV", 0, 0, 2, caller)) {
    case 1:
        return IRComponents::TextAlignV::CENTER;
    case 2:
        return IRComponents::TextAlignV::BOTTOM;
    default:
        return IRComponents::TextAlignV::TOP;
    }
}

inline constexpr std::array<std::string_view, 9> kCanvasDrawTextKeys{
    "color",
    "fontSize",
    "size",
    "distance",
    "alignH",
    "alignV",
    "boxWidth",
    "boxHeight",
    "wrapWidth",
};
inline constexpr std::array<std::string_view, 12> kCanvasDrawVoxelTextKeys{
    "color",
    "faceX",
    "faceY",
    "faceZ",
    "voxelSize",
    "depth",
    "facing",
    "distance",
    "alignH",
    "alignV",
    "boxWidth",
    "boxHeight",
};
inline constexpr std::array<std::string_view, 3> kCanvasDrawMeasureKeys{
    "voxelSize", "depth", "facing"
};
inline constexpr std::array<std::string_view, 7> kCanvasDrawVoxelRunKeys{
    "count",
    "lit",
    "voxelSize",
    "axis",
    "color",
    "unlitColor",
    "distance",
};
inline constexpr std::array<std::string_view, 3> kCanvasDrawRunMeasureKeys{
    "count", "voxelSize", "axis"
};
inline constexpr std::array<std::string_view, 6> kCanvasDrawImageKeys{
    "uv", "cell", "tint", "levels", "rotation", "distance"
};

// The `rotation` of an image draw: radians clockwise about the box's centre,
// none when absent.
inline float canvasDrawRotation(const sol::table &options, const char *caller) {
    const sol::object value = options["rotation"];
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return 0.0f;
    }
    const std::string message =
        std::string(caller) + ": rotation must be a finite number of radians";
    if (value.get_type() != sol::type::number) {
        throw sol::error{message};
    }
    const float radians = static_cast<float>(value.as<double>());
    if (!IRMath::isFinite(radians)) {
        throw sol::error{message};
    }
    return radians;
}

// The crop, mosaic cell, posterize count, tint and rotation of an image draw
// from `options`, whose keys the caller has already checked.
inline IRPrefab::CanvasDraw::ImageOptions
canvasDrawImageOptions(const sol::table &options, const char *caller) {
    IRPrefab::CanvasDraw::ImageOptions imageOptions;
    imageOptions.uvRect_ = canvasDrawUvRect(options, caller);
    imageOptions.cell_ =
        integerOption(options, "cell", 1, 1, IRPrefab::CanvasDraw::kMaxImageCell, caller);
    imageOptions.tint_ = colorOption(options, "tint", imageOptions.tint_);
    imageOptions.levels_ = integerOption(
        options,
        "levels",
        0,
        0,
        IRPrefab::CanvasDraw::kMaxImagePosterizeLevels,
        caller
    );
    if (imageOptions.levels_ == 1) {
        throw sol::error{
            std::string(caller) + ": levels must be 0 (off) or a whole number in [2, " +
            std::to_string(IRPrefab::CanvasDraw::kMaxImagePosterizeLevels) + "]"
        };
    }
    imageOptions.rotation_ = canvasDrawRotation(options, caller);
    return imageOptions;
}

// An `IRCanvas.Axis` value, X when absent.
inline IRRender::VoxelRunAxis canvasDrawAxis(const sol::table &options, const char *caller) {
    const int value = integerOption(options, "axis", 0, 0, 2, caller);
    switch (value) {
    case static_cast<int>(IRRender::VoxelRunAxis::Y):
        return IRRender::VoxelRunAxis::Y;
    case static_cast<int>(IRRender::VoxelRunAxis::SCREEN):
        return IRRender::VoxelRunAxis::SCREEN;
    default:
        return IRRender::VoxelRunAxis::X;
    }
}

// The count, cube size and axis of a voxel run from `options`.
inline IRPrefab::CanvasDraw::VoxelRunOptions
canvasDrawRunShape(const sol::table &options, const char *caller) {
    IRPrefab::CanvasDraw::VoxelRunOptions run;
    run.count_ = integerOption(options, "count", 1, 1, 4096, caller);
    run.voxelSize_ = integerOption(options, "voxelSize", 1, 1, IRRender::kMaxVoxelTextSize, caller);
    run.axis_ = canvasDrawAxis(options, caller);
    if (!IRRender::voxelRunFits(run.count_, run.voxelSize_, run.axis_)) {
        throw sol::error{
            std::string(caller) + ": count and voxelSize make a run larger than " +
            std::to_string(IRRender::kMaxVoxelRunTexels) + " texels"
        };
    }
    return run;
}

inline IRRender::VoxelTextStyle
canvasDrawVoxelStyle(const sol::table &options, const char *caller) {
    IRRender::VoxelTextStyle style{1, 1, IRRender::VoxelTextFacing::CAMERA};
    style.voxelSize_ =
        integerOption(options, "voxelSize", 1, 1, IRRender::kMaxVoxelTextSize, caller);
    style.depth_ = integerOption(options, "depth", 1, 1, IRRender::kMaxVoxelTextDepth, caller);
    const sol::object facing = options["facing"];
    if (facing.valid() && facing.get_type() != sol::type::lua_nil) {
        style.facing_ = voxelFacingFromLua(facing, caller);
    }
    return style;
}

/// @p imageBase confines every `loadImage` path, as `bindWidgets`' image base
/// does; it is canonicalized once here, and a base that cannot be resolved
/// throws. @p resolve names a canvas for the draw calls; @p createLayer stands
/// up a layer for `createLayer`, returning kNullEntity when the name is taken;
/// @p loadImage decodes a contained path; @p framebufferSize answers
/// `framebufferSize`.
inline void bindCanvasDraw(
    LuaScript &script,
    const std::filesystem::path &imageBase,
    CanvasResolver resolve = renderCanvasByName,
    LayerFactory createLayer = renderLayer,
    ImageLoader loadImage = renderImage,
    FramebufferSizeResolver framebufferSize = renderFramebufferSize
) {
    std::error_code baseError;
    const std::filesystem::path canonicalImageBase =
        std::filesystem::weakly_canonical(imageBase, baseError);
    if (baseError) {
        throw std::invalid_argument("bindCanvasDraw requires a valid image base");
    }

    sol::state &lua = script.lua();
    const sol::object existing = lua["IRCanvas"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRCanvas must be a table before bindLuaCanvasDraw()");
    }
    sol::table canvas =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();
    lua["IRCanvas"] = canvas;

    sol::table depth = lua.create_table();
    depth["BEHIND"] = IRPrefab::CanvasDraw::kDepthBehind;
    depth["WORLD_NEAR"] = IRPrefab::CanvasDraw::kDepthWorldNear;
    depth["WORLD_FAR"] = IRPrefab::CanvasDraw::kDepthWorldFar;
    depth["FRONT"] = IRPrefab::CanvasDraw::kDepthFront;
    canvas["Depth"] = depth;

    sol::table axis = lua.create_table();
#define IR_BIND_RUN_AXIS(name) axis[#name] = static_cast<lua_Integer>(IRRender::VoxelRunAxis::name)
    IR_BIND_RUN_AXIS(X);
    IR_BIND_RUN_AXIS(Y);
    IR_BIND_RUN_AXIS(SCREEN);
#undef IR_BIND_RUN_AXIS
    canvas["Axis"] = axis;

    sol::table alignH = lua.create_table();
#define IR_BIND_ALIGN_H(name)                                                                      \
    alignH[#name] = static_cast<lua_Integer>(IRComponents::TextAlignH::name)
    IR_BIND_ALIGN_H(LEFT);
    IR_BIND_ALIGN_H(CENTER);
    IR_BIND_ALIGN_H(RIGHT);
#undef IR_BIND_ALIGN_H
    canvas["AlignH"] = alignH;
    sol::table alignV = lua.create_table();
#define IR_BIND_ALIGN_V(name)                                                                      \
    alignV[#name] = static_cast<lua_Integer>(IRComponents::TextAlignV::name)
    IR_BIND_ALIGN_V(TOP);
    IR_BIND_ALIGN_V(CENTER);
    IR_BIND_ALIGN_V(BOTTOM);
#undef IR_BIND_ALIGN_V
    canvas["AlignV"] = alignV;

    const auto require = [resolve](const std::string &name, const char *caller) {
        const IREntity::EntityId entity = resolve(name);
        if (entity == IREntity::kNullEntity) {
            throw sol::error{std::string(caller) + ": no canvas named " + name};
        }
        return entity;
    };

    canvas["createLayer"] = [createLayer](const std::string &name) {
        if (name.empty()) {
            throw sol::error{"IRCanvas.createLayer: the name must not be empty"};
        }
        if (createLayer(name) == IREntity::kNullEntity) {
            throw sol::error{"IRCanvas.createLayer: a canvas named " + name + " exists"};
        }
    };

    canvas["size"] = [require](const std::string &name) {
        const IRMath::ivec2 size =
            IREntity::getComponent<IRComponents::C_SizeTriangles>(require(name, "IRCanvas.size"))
                .size_;
        return std::make_tuple(size.x, size.y);
    };

    canvas["text"] = [require](
                         sol::this_state state,
                         const std::string &name,
                         const std::string &text,
                         int x,
                         int y,
                         sol::object optionsObject
                     ) {
        constexpr const char *kCaller = "IRCanvas.text";
        const IREntity::EntityId target = require(name, kCaller);
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawTextKeys, kCaller);
        IRPrefab::CanvasDraw::TextOptions textOptions;
        textOptions.color_ = colorOption(options, "color", textOptions.color_);
        textOptions.fontSize_ = integerOption(options, "fontSize", 1, 1, 16, kCaller);
        textOptions.size_ = textSizeFromLua(options["size"], kCaller);
        textOptions.alignH_ = canvasDrawAlignH(options, kCaller);
        textOptions.alignV_ = canvasDrawAlignV(options, kCaller);
        textOptions.boxWidth_ = integerOption(options, "boxWidth", 0, 0, 65535, kCaller);
        textOptions.boxHeight_ = integerOption(options, "boxHeight", 0, 0, 65535, kCaller);
        textOptions.wrapWidth_ = integerOption(options, "wrapWidth", 0, -1, 65535, kCaller);
        IRPrefab::CanvasDraw::queueText(
            target,
            text,
            IRMath::ivec2(x, y),
            textOptions,
            canvasDrawDistance(options, kCaller)
        );
    };

    canvas["voxelText"] = [require](
                              sol::this_state state,
                              const std::string &name,
                              const std::string &text,
                              int x,
                              int y,
                              sol::object optionsObject
                          ) {
        constexpr const char *kCaller = "IRCanvas.voxelText";
        const IREntity::EntityId target = require(name, kCaller);
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawVoxelTextKeys, kCaller);
        IRPrefab::CanvasDraw::VoxelTextOptions voxelOptions;
        voxelOptions.style_ = canvasDrawVoxelStyle(options, kCaller);
        // `color` shades all three faces from one top colour; an explicit face
        // colour then overrides its face.
        const sol::object top = options["color"];
        if (top.valid() && top.get_type() != sol::type::lua_nil) {
            voxelOptions.faces_ = IRRender::shadedVoxelFaceColors(colorFromLua(top));
        }
        voxelOptions.faces_.x_ = colorOption(options, "faceX", voxelOptions.faces_.x_);
        voxelOptions.faces_.y_ = colorOption(options, "faceY", voxelOptions.faces_.y_);
        voxelOptions.faces_.z_ = colorOption(options, "faceZ", voxelOptions.faces_.z_);
        voxelOptions.alignH_ = canvasDrawAlignH(options, kCaller);
        voxelOptions.alignV_ = canvasDrawAlignV(options, kCaller);
        voxelOptions.boxWidth_ = integerOption(options, "boxWidth", 0, 0, 65535, kCaller);
        voxelOptions.boxHeight_ = integerOption(options, "boxHeight", 0, 0, 65535, kCaller);
        IRPrefab::CanvasDraw::queueVoxelText(
            target,
            text,
            IRMath::ivec2(x, y),
            voxelOptions,
            canvasDrawDistance(options, kCaller)
        );
    };

    canvas["measureVoxelText"] = [](sol::this_state state,
                                    const std::string &text,
                                    sol::object optionsObject) {
        constexpr const char *kCaller = "IRCanvas.measureVoxelText";
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawMeasureKeys, kCaller);
        const IRMath::ivec2 size =
            IRPrefab::CanvasDraw::measureVoxelText(text, canvasDrawVoxelStyle(options, kCaller));
        return std::make_tuple(size.x, size.y);
    };

    canvas["voxelRun"] = [require](
                             sol::this_state state,
                             const std::string &name,
                             int x,
                             int y,
                             sol::object optionsObject
                         ) {
        constexpr const char *kCaller = "IRCanvas.voxelRun";
        const IREntity::EntityId target = require(name, kCaller);
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawVoxelRunKeys, kCaller);
        IRPrefab::CanvasDraw::VoxelRunOptions run = canvasDrawRunShape(options, kCaller);
        run.lit_ = integerOption(options, "lit", 0, 0, run.count_, kCaller);
        // Each group shades its three faces from one top colour.
        const sol::object lit = options["color"];
        if (lit.valid() && lit.get_type() != sol::type::lua_nil) {
            run.lit_faces_ = IRRender::shadedVoxelFaceColors(colorFromLua(lit));
        }
        const sol::object unlit = options["unlitColor"];
        if (unlit.valid() && unlit.get_type() != sol::type::lua_nil) {
            run.unlit_faces_ = IRRender::shadedVoxelFaceColors(colorFromLua(unlit));
        }
        IRPrefab::CanvasDraw::queueVoxelRun(
            target,
            IRMath::ivec2(x, y),
            run,
            canvasDrawDistance(options, kCaller)
        );
    };

    canvas["measureVoxelRun"] = [](sol::this_state state, sol::object optionsObject) {
        constexpr const char *kCaller = "IRCanvas.measureVoxelRun";
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawRunMeasureKeys, kCaller);
        const IRPrefab::CanvasDraw::VoxelRunOptions run = canvasDrawRunShape(options, kCaller);
        const IRMath::ivec2 size = IRRender::voxelRunSize(run.count_, run.voxelSize_, run.axis_);
        return std::make_tuple(size.x, size.y);
    };

    // The rect of cube `index` (1-based) inside the run's box: x, y, w, h.
    canvas["voxelRunCell"] = [](sol::this_state state, int index, sol::object optionsObject) {
        constexpr const char *kCaller = "IRCanvas.voxelRunCell";
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawRunMeasureKeys, kCaller);
        const IRPrefab::CanvasDraw::VoxelRunOptions run = canvasDrawRunShape(options, kCaller);
        if (index < 1 || index > run.count_) {
            throw sol::error{std::string(kCaller) + ": index must be 1..count"};
        }
        IRMath::ivec2 pos;
        IRMath::ivec2 size;
        IRRender::voxelRunCellRect(run.count_, run.voxelSize_, run.axis_, index - 1, pos, size);
        return std::make_tuple(pos.x, pos.y, size.x, size.y);
    };

    canvas["fillRect"] = [require](
                             const std::string &name,
                             int x,
                             int y,
                             int w,
                             int h,
                             sol::object color,
                             sol::optional<int> distance
                         ) {
        IRPrefab::CanvasDraw::queueRect(
            require(name, "IRCanvas.fillRect"),
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            colorFromLua(color),
            distance.value_or(IRPrefab::CanvasDraw::kDepthFront)
        );
    };

    canvas["drawBorder"] = [require](
                               const std::string &name,
                               int x,
                               int y,
                               int w,
                               int h,
                               sol::object color,
                               sol::optional<int> thickness,
                               sol::optional<int> distance
                           ) {
        IRPrefab::CanvasDraw::queueBorder(
            require(name, "IRCanvas.drawBorder"),
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            colorFromLua(color),
            distance.value_or(IRPrefab::CanvasDraw::kDepthFront),
            thickness.value_or(1)
        );
    };

    // `path` is relative to the image base, or an absolute path the script's
    // grants hold (what the user picked in `IRDialog.open`). The same file
    // loads once: a second load returns the first handle and counts one more
    // reference, which `releaseImage` gives back. The grant set is a LuaScript
    // member that outlives this closure.
    const PathGrants *grants = &script.pathGrants();
    canvas["loadImage"] = statefulLuaFunction(
        [canonicalImageBase, loadImage, grants](
            const std::string &path
        ) -> std::tuple<sol::optional<lua_Integer>, sol::optional<std::string>> {
            constexpr const char *kCaller = "IRCanvas.loadImage";
            const std::optional<std::filesystem::path> resolved =
                resolveReadablePath(*grants, canonicalImageBase, path);
            if (!resolved) {
                return {
                    sol::nullopt,
                    std::string(kCaller) + ": path '" + path +
                        "' must be relative to the image base and stay inside it (no '..', no "
                        "symlink out), or an absolute path the user picked in IRDialog.open"
                };
            }
            std::string error;
            const IRPrefab::CanvasDraw::ImageHandle handle = loadImage(resolved->string(), error);
            if (handle == IRPrefab::CanvasDraw::kInvalidImage) {
                return {sol::nullopt, std::string(kCaller) + ": " + error};
            }
            return {static_cast<lua_Integer>(handle), sol::nullopt};
        }
    );

    canvas["imageSize"] = [](sol::object handle) {
        const IRMath::ivec2 size =
            IRPrefab::CanvasDraw::imageSize(requireCanvasDrawImage(handle, "IRCanvas.imageSize"));
        return std::make_tuple(size.x, size.y);
    };

    // A handle already released is a no-op, so a teardown may release twice.
    canvas["releaseImage"] = [](sol::object handle) {
        const IRPrefab::CanvasDraw::ImageHandle image = canvasDrawImageHandle(handle);
        if (image == IRPrefab::CanvasDraw::kInvalidImage) {
            throw sol::error{"IRCanvas.releaseImage: handle must be IRCanvas.loadImage's result"};
        }
        IRPrefab::CanvasDraw::releaseImage(image);
    };

    canvas["image"] = statefulLuaFunction([require](
                                              sol::this_state state,
                                              const std::string &name,
                                              sol::object handle,
                                              int x,
                                              int y,
                                              int w,
                                              int h,
                                              sol::object optionsObject
                                          ) {
        constexpr const char *kCaller = "IRCanvas.image";
        const IREntity::EntityId target = require(name, kCaller);
        const IRPrefab::CanvasDraw::ImageHandle image = requireCanvasDrawImage(handle, kCaller);
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawImageKeys, kCaller);
        const IRPrefab::CanvasDraw::ImageOptions imageOptions =
            canvasDrawImageOptions(options, kCaller);
        IRPrefab::CanvasDraw::queueImage(
            target,
            image,
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            imageOptions,
            canvasDrawDistance(options, kCaller)
        );
    });

    canvas["framebufferSize"] = statefulLuaFunction([framebufferSize]() {
        const IRMath::ivec2 size = framebufferSize();
        return std::make_tuple(size.x, size.y);
    });

    canvas["framebufferImage"] = [](sol::this_state state,
                                    sol::object handle,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    sol::object optionsObject) {
        constexpr const char *kCaller = "IRCanvas.framebufferImage";
        const IRPrefab::CanvasDraw::ImageHandle image = requireCanvasDrawImage(handle, kCaller);
        const sol::table options = optionsTable(state, optionsObject, kCaller);
        rejectUnknownKeys(options, kCanvasDrawImageKeys, kCaller);
        const IRPrefab::CanvasDraw::ImageOptions imageOptions =
            canvasDrawImageOptions(options, kCaller);
        IRPrefab::CanvasDraw::queueFramebufferImage(
            image,
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            imageOptions,
            canvasDrawDistance(options, kCaller)
        );
    };
}

// The image base defaults to the run directory, the tree `IRFile` reads.
inline void bindCanvasDraw(
    LuaScript &script,
    CanvasResolver resolve = renderCanvasByName,
    LayerFactory createLayer = renderLayer
) {
    std::error_code error;
    const std::filesystem::path runDirectory = std::filesystem::current_path(error);
    if (error) {
        throw std::runtime_error("bindCanvasDraw could not resolve the run directory");
    }
    bindCanvasDraw(script, runDirectory, std::move(resolve), std::move(createLayer));
}

} // namespace IRScript::detail

#endif /* LUA_CANVAS_DRAW_BINDINGS_H */
