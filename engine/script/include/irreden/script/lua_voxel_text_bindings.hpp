#ifndef LUA_VOXEL_TEXT_BINDINGS_H
#define LUA_VOXEL_TEXT_BINDINGS_H

// IRText voxel titles — world-space text built from voxel letters
// (`IRPrefab::VoxelText`; contract in docs/design/voxel-text.md).
//
//   local root, letters = IRText.createVoxel(text, opts?)
//   IRText.removeVoxel(root)
//
// opts, every field optional:
//   origin  {x, y, z}, vec3 or ivec3: the first letter's bottom-centre cell
//   color   Color or {r, g, b[, a]} (white)
//   depth   integer layers, 1..IRPrefab::VoxelText::kMaxDepth (2)
//   scale   integer voxels per glyph column, 1..kMaxScale (1)
//   path    a heading in radians about +Z (0 = +X), a list of headings (one
//           per letter step, the last repeating), or a list of world points
//           ({x, y, z} tables, vec3 or ivec3) of a polyline from `origin`;
//           absent: the diagonal that reads rising to the right on screen
//   canvas  an entity carrying a C_VoxelPool (the active canvas)
// Letters face the main camera's yaw at the call, snapped to a multiple of
// π/4. `letters` counts the letter entities: every character but a space.
// Each invalid argument, an unknown option or a newline in `text` raises
// `IRText.createVoxel: ...` before any entity exists. `root` is an integer
// entity id; removeVoxel destroys its tree. Both verbs change archetypes
// eagerly, so a Lua system tick wraps them in IREntity.deferredCall.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/voxel_text.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_hierarchy_bindings.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace IRScript {

namespace detail {

inline sol::error voxelTextError(const std::string &message) {
    return sol::error{"IRText.createVoxel: " + message};
}

inline bool isAbsentOrNil(const sol::object &value) {
    return !value.valid() || value.get_type() == sol::type::lua_nil;
}

// Coordinates past 2^24 are no longer whole floats, and a NaN or an infinity
// would reach the integer cell rounding.
constexpr float kVoxelTextMaxCoordinate = 16777216.0f;

inline bool isVoxelTextCoordinate(float value) {
    return IRMath::abs(value) <= kVoxelTextMaxCoordinate;
}

inline IRMath::vec3 voxelTextPoint(const sol::object &value, const std::string &name) {
    IRMath::vec3 point;
    if (value.is<IRMath::ivec3>()) {
        point = IRMath::vec3(value.as<IRMath::ivec3>());
    } else if (value.is<IRMath::vec3>() || value.get_type() == sol::type::table) {
        point = vec3FromLua(value);
    } else {
        throw voxelTextError(name + " must be an {x, y, z} table, a vec3 or an ivec3");
    }
    if (!isVoxelTextCoordinate(point.x) || !isVoxelTextCoordinate(point.y) ||
        !isVoxelTextCoordinate(point.z)) {
        throw voxelTextError(name + " must have finite coordinates within 2^24");
    }
    return point;
}

inline int voxelTextCount(const sol::object &value, const char *name, int fallback, int maximum) {
    if (isAbsentOrNil(value)) {
        return fallback;
    }
    if (value.get_type() == sol::type::number) {
        const double number = value.as<double>();
        // Bounded before the cast: converting NaN or an out-of-range double is
        // undefined behaviour.
        if (number >= 1.0 && number <= static_cast<double>(maximum) &&
            static_cast<double>(static_cast<int>(number)) == number) {
            return static_cast<int>(number);
        }
    }
    throw voxelTextError(
        std::string{name} + " must be an integer from 1 to " + std::to_string(maximum)
    );
}

inline float voxelTextHeading(const sol::object &value) {
    const double heading = value.as<double>();
    if (!(IRMath::abs(heading) <= static_cast<double>(kVoxelTextMaxCoordinate))) {
        throw voxelTextError("path headings must be finite");
    }
    return static_cast<float>(heading);
}

inline IRPrefab::VoxelText::VoxelTextPath voxelTextPath(const sol::object &value) {
    IRPrefab::VoxelText::VoxelTextPath path;
    if (isAbsentOrNil(value)) {
        return path;
    }
    if (value.get_type() == sol::type::number) {
        path.headings_.push_back(voxelTextHeading(value));
        return path;
    }
    constexpr const char *kShape = "path must be a heading, a list of headings or a list of points";
    if (value.get_type() != sol::type::table) {
        throw voxelTextError(kShape);
    }
    const sol::table list = value.as<sol::table>();
    const std::size_t count = list.size();
    if (count == 0) {
        throw voxelTextError("path must not be empty");
    }
    const bool headings = list.get<sol::object>(1).get_type() == sol::type::number;
    for (std::size_t i = 1; i <= count; ++i) {
        const sol::object entry = list.get<sol::object>(i);
        if ((entry.get_type() == sol::type::number) != headings) {
            throw voxelTextError(std::string{kShape} + ", not a mix");
        }
        if (headings) {
            path.headings_.push_back(voxelTextHeading(entry));
        } else {
            path.points_.push_back(voxelTextPoint(entry, "path point " + std::to_string(i)));
        }
    }
    return path;
}

inline IREntity::EntityId voxelTextCanvas(const sol::object &value) {
    if (isAbsentOrNil(value)) {
        return IREntity::kNullEntity;
    }
    const IREntity::EntityId canvas = requireLiveEntity(value, "IRText", "createVoxel", "canvas");
    if (!IREntity::getComponentOptional<IRComponents::C_VoxelPool>(canvas).has_value()) {
        throw voxelTextError("canvas has no C_VoxelPool");
    }
    return canvas;
}

constexpr std::array<std::string_view, 6> kVoxelTextOptionNames{
    "origin", "color", "depth", "scale", "path", "canvas"
};

inline IRPrefab::VoxelText::VoxelTextOptions voxelTextOptions(const sol::object &value) {
    using IRPrefab::VoxelText::kMaxDepth;
    using IRPrefab::VoxelText::kMaxScale;
    IRPrefab::VoxelText::VoxelTextOptions options;
    if (isAbsentOrNil(value)) {
        return options;
    }
    if (value.get_type() != sol::type::table) {
        throw voxelTextError("opts must be a table");
    }
    const sol::table opts = value.as<sol::table>();
    for (const auto &[key, entry] : opts) {
        const std::string name = key.get_type() == sol::type::string ? key.as<std::string>() : "";
        if (std::find(kVoxelTextOptionNames.begin(), kVoxelTextOptionNames.end(), name) ==
            kVoxelTextOptionNames.end()) {
            throw voxelTextError(
                "unknown option" + (name.empty() ? std::string{} : " '" + name + "'")
            );
        }
    }

    const sol::object origin = opts["origin"];
    if (!isAbsentOrNil(origin)) {
        options.origin_ = voxelTextPoint(origin, "origin");
    }
    const sol::object color = opts["color"];
    if (!isAbsentOrNil(color)) {
        if (!color.is<IRMath::Color>() && color.get_type() != sol::type::table) {
            throw voxelTextError("color must be a Color or an {r, g, b[, a]} table");
        }
        options.color_ = colorFromLua(color);
    }
    options.depth_ = voxelTextCount(opts["depth"], "depth", options.depth_, kMaxDepth);
    options.scale_ = voxelTextCount(opts["scale"], "scale", options.scale_, kMaxScale);
    options.path_ = voxelTextPath(opts["path"]);
    if (!options.path_.points_.empty()) {
        const std::vector<IRMath::vec3> vertices = IRPrefab::VoxelText::detail::polylineVertices(
            options.path_.points_,
            IRMath::roundVec3HalfUp(options.origin_)
        );
        if (vertices.size() < 2) {
            throw voxelTextError("path points must leave the origin");
        }
        // Letters past the polyline's end continue along its last segment,
        // which must move on screen at the yaw the letters will face.
        const float yaw = IRPrefab::Camera::getYaw();
        if (!IRPrefab::VoxelText::detail::movesOnScreen(
                vertices.back() - vertices[vertices.size() - 2],
                yaw
            )) {
            throw voxelTextError("path's last segment runs along the camera's view axis");
        }
        options.cameraYaw_ = yaw;
    }
    options.canvas_ = voxelTextCanvas(opts["canvas"]);
    return options;
}

inline void bindVoxelText(LuaScript &script) {
    sol::table text = script.lua()["IRText"];

    text["createVoxel"] = [](const std::string &str, sol::object opts) {
        if (str.find('\n') != std::string::npos) {
            throw voxelTextError("text must be a single line");
        }
        const IRPrefab::VoxelText::VoxelTextOptions options = voxelTextOptions(opts);
        const IREntity::EntityId root = IRPrefab::VoxelText::create(str, options);
        const auto letters =
            static_cast<int>(IREntity::getEntityManager().getChildren(root).size());
        return std::make_tuple(root, letters);
    };

    text["removeVoxel"] = [](sol::object root) {
        IREntity::getEntityManager().destroyTree(
            requireLiveEntity(root, "IRText", "removeVoxel", "root")
        );
    };
}

} // namespace detail

} // namespace IRScript

#endif /* LUA_VOXEL_TEXT_BINDINGS_H */
