#ifndef LUA_RENDER_BINDINGS_H
#define LUA_RENDER_BINDINGS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace IRScript::detail {

// IRRender + IRGui shared Lua bindings.
//
// Render-glue setters (sun direction / intensity / ambient, sky color /
// intensity), the main-camera zoom and its policy, the LOD tier read, the
// framebuffer glitch state, and a
// minimal GUI-canvas shape-draw primitive
// (filled disc, line). Bound by `bindLuaDrivenEcs()` so every creation on the Lua-first
// authoring path gets them without re-declaring per-creation pass-throughs
// (the duplication this issue retires). Every binding is a thin forward to an
// existing `IRRender::` entry point — no render logic lives here.
//
// `setSunDirection` forwards to `IRRender::setSunDirection`, which preserves
// the engine-side `dir.z <= 0` assert and normalizes on write. There is no
// clear / background-color setter engine-side; `setSkyColor` (the sky
// hemisphere term) is the closest available color knob.
//
// The shape draws forward to `IRRender::drawGuiDisc` / `drawGuiLine`, which
// own the "gui"-canvas resolution and rasterization. They are IMMEDIATE-MODE:
// the GUI canvas is cleared every frame by `TEXT_TO_TRIXEL`, so a draw only
// persists on screen if it is re-issued each frame from a RENDER-phase Lua
// system positioned after that clear — the same contract the widget render
// systems follow. A one-shot draw at script-load time is wiped on the next
// frame that clears the canvas.
// `IRRender.setGlitch` keys. A key outside this list raises, so a typo cannot
// silently leave a field unchanged.
inline constexpr std::array<std::string_view, 11> kGlitchKeys{
    "intensity",
    "period",
    "phase",
    "seed",
    "tear",
    "hold",
    "split",
    "bars",
    "cells",
    "depth",
    "holdDivisor",
};

// Builds the candidate a `setGlitch` table describes on top of the current
// state. Raises on the first bad key or value; nothing is stored here.
inline IRComponents::C_GlitchState glitchCandidateFromLua(sol::table settings) {
    for (const auto &entry : settings) {
        const sol::object &key = entry.first;
        if (key.get_type() != sol::type::string ||
            std::find(kGlitchKeys.begin(), kGlitchKeys.end(), key.as<std::string_view>()) ==
                kGlitchKeys.end()) {
            throw sol::error{
                "IRRender.setGlitch: unknown key (expected intensity, period, phase, seed, "
                "tear, hold, split, bars, cells, depth or holdDivisor)"
            };
        }
    }
    const auto number = [&settings](const char *key, double &out) {
        const sol::object value = settings[key];
        if (!value.valid()) {
            return false;
        }
        if (value.get_type() != sol::type::number || !IRMath::isFinite(value.as<double>())) {
            throw sol::error{
                std::string("IRRender.setGlitch: ") + key + " must be a finite number"
            };
        }
        out = value.as<double>();
        return true;
    };
    const auto unit = [&number](const char *key, float &field) {
        double value = 0.0;
        if (!number(key, value)) {
            return;
        }
        if (value < 0.0 || value > 1.0) {
            throw sol::error{std::string("IRRender.setGlitch: ") + key + " must be in [0, 1]"};
        }
        field = static_cast<float>(value);
    };
    const auto integer = [&number](const char *key, double lo, double hi, double &out) {
        if (!number(key, out)) {
            return false;
        }
        if (out != IRMath::floor(out) || out < lo || out > hi) {
            throw sol::error{
                std::string("IRRender.setGlitch: ") + key + " must be an integer in [" +
                std::to_string(static_cast<long long>(lo)) + ", " +
                std::to_string(static_cast<long long>(hi)) + "]"
            };
        }
        return true;
    };

    IRComponents::C_GlitchState candidate = IRPrefab::Glitch::stateOrDefault();
    unit("intensity", candidate.intensity_);
    unit("tear", candidate.tearWeight_);
    unit("hold", candidate.holdWeight_);
    unit("split", candidate.splitWeight_);
    unit("bars", candidate.barsWeight_);
    unit("cells", candidate.cellsWeight_);
    unit("depth", candidate.depthWeight_);
    double value = 0.0;
    if (number("period", value)) {
        candidate.periodSeconds_ = value;
    }
    if (number("phase", value)) {
        candidate.phaseSeconds_ = value;
    }
    if (integer(
            "seed",
            0.0,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
            value
        )) {
        candidate.seed_ = static_cast<std::uint32_t>(value);
    }
    if (integer("holdDivisor", 1.0, static_cast<double>(std::numeric_limits<int>::max()), value)) {
        candidate.holdDivisor_ = static_cast<int>(value);
    }
    return candidate;
}

inline void bindRenderGlue(LuaScript &script) {
    sol::state &lua = script.lua();

    // Extend (never replace) IRRender so a creation that also adds its own
    // IRRender entries (e.g. getGuiScale, measureText) keeps them.
    if (!lua["IRRender"].valid()) {
        lua["IRRender"] = lua.create_table();
    }

    lua["IRRender"]["setSunDirection"] = [](float x, float y, float z) {
        IRRender::setSunDirection(IRMath::vec3(x, y, z));
    };
    lua["IRRender"]["setSunIntensity"] = [](float intensity) {
        IRRender::setSunIntensity(intensity);
    };
    lua["IRRender"]["setSunAmbient"] = [](float ambient) {
        IRRender::setSunAmbient(ambient);
    };
    lua["IRRender"]["setSkyColor"] = [](float r, float g, float b) {
        IRRender::setSkyColor(IRMath::vec3(r, g, b));
    };
    lua["IRRender"]["setSkyIntensity"] = [](float intensity) {
        IRRender::setSkyIntensity(intensity);
    };
    lua["IRRender"]["entityCanvasCount"] = []() { return IRPrefab::EntityCanvas::count(); };
    lua["IRRender"]["canAllocateVoxels"] = [](unsigned int size) {
        return IRPrefab::VoxelPool::canAllocate(size, IRRender::getActiveCanvasEntityOrNull());
    };

    // Main-camera zoom. The value setter and getter forward to the core
    // `IRRender::` pair; the policy pair forwards to `IRPrefab::Camera`, which
    // owns what a write means under each policy. Arguments are checked here,
    // ahead of any write, so a rejected call leaves both the zoom and the
    // policy as they were.
    lua["IRRender"]["setCameraZoom"] = [](sol::object zoom) {
        if (zoom.get_type() != sol::type::number || !std::isfinite(zoom.as<double>())) {
            throw sol::error{"IRRender.setCameraZoom: expected a finite number"};
        }
        IRRender::setCameraZoom(zoom.as<float>());
    };
    lua["IRRender"]["getCameraZoom"] = []() { return IRRender::getCameraZoom().x; };
    lua["IRRender"]["setCameraZoomContinuous"] = [](sol::object continuous) {
        if (continuous.get_type() != sol::type::boolean) {
            throw sol::error{"IRRender.setCameraZoomContinuous: expected a boolean"};
        }
        IRPrefab::Camera::setZoomContinuous(continuous.as<bool>());
    };
    lua["IRRender"]["isCameraZoomContinuous"] = []() {
        return IRPrefab::Camera::isZoomContinuous();
    };

    // LOD tier surface. Tier index goes down as detail goes up (LOD_0 finest,
    // LOD_4 coarsest). `getActiveLodTier` reads the singleton LOD_UPDATE writes
    // and reports LOD_4 when no creation registered that system, matching what
    // the tier consumers resolve. A CODEGEN tick body reaches the same
    // `IRRender::getActiveLodTier` through the intrinsic whitelist.
    sol::table lodLevel = lua.create_table();
#define IR_BIND_LOD(name) lodLevel[#name] = static_cast<lua_Integer>(IRRender::LodLevel::name)
    IR_BIND_LOD(LOD_0);
    IR_BIND_LOD(LOD_1);
    IR_BIND_LOD(LOD_2);
    IR_BIND_LOD(LOD_3);
    IR_BIND_LOD(LOD_4);
#undef IR_BIND_LOD
    lua["IRRender"]["LodLevel"] = lodLevel;
    lua["IRRender"]["getActiveLodTier"] = []() -> lua_Integer {
        return IRRender::getActiveLodTier();
    };

    // Framebuffer glitch. `setGlitch` is a partial update: omitted keys keep
    // their values, and every key is checked before any field changes.
    // `glitch()` returns a fresh table of the settings plus the engine-clock
    // `timeSeconds`, `tick` and `holdTick`, so a script can set `phase` from
    // the event it locks to and act on the same tick as the pass.
    lua["IRRender"]["setGlitch"] = [](sol::object settings) {
        if (settings.get_type() != sol::type::table) {
            throw sol::error{"IRRender.setGlitch: expected a table"};
        }
        const IRComponents::C_GlitchState candidate =
            glitchCandidateFromLua(settings.as<sol::table>());
        const std::string error =
            IRPrefab::Glitch::validate(candidate, IRPrefab::Glitch::engineTimeSeconds());
        if (!error.empty()) {
            throw sol::error{"IRRender.setGlitch: " + error};
        }
        IRPrefab::Glitch::setState(candidate);
    };
    lua["IRRender"]["glitch"] = [](sol::this_state state) {
        sol::state_view view{state};
        const IRComponents::C_GlitchState current = IRPrefab::Glitch::stateOrDefault();
        const IRPrefab::Glitch::Clock clock =
            IRPrefab::Glitch::clockAt(current, IRPrefab::Glitch::engineTimeSeconds());
        sol::table out = view.create_table();
        out["intensity"] = current.intensity_;
        out["period"] = current.periodSeconds_;
        out["phase"] = current.phaseSeconds_;
        out["seed"] = static_cast<double>(current.seed_);
        out["tear"] = current.tearWeight_;
        out["hold"] = current.holdWeight_;
        out["split"] = current.splitWeight_;
        out["bars"] = current.barsWeight_;
        out["cells"] = current.cellsWeight_;
        out["depth"] = current.depthWeight_;
        out["holdDivisor"] = current.holdDivisor_;
        out["timeSeconds"] = clock.timeSeconds_;
        out["tick"] = static_cast<double>(clock.tick_);
        out["holdTick"] = static_cast<double>(clock.holdTick_);
        return out;
    };

    if (!lua["IRGui"].valid()) {
        lua["IRGui"] = lua.create_table();
    }

    lua["IRGui"]["drawDisc"] = [](int x, int y, int radius, sol::object color) {
        IRRender::drawGuiDisc(IRMath::ivec2(x, y), radius, colorFromLua(color));
    };

    lua["IRGui"]["drawLine"] = [](int x0, int y0, int x1, int y1, sol::object color) {
        IRRender::drawGuiLine(
            IRMath::ivec2(x0, y0),
            IRMath::ivec2(x1, y1),
            colorFromLua(color)
        );
    };

    lua["IRGui"]["fillRect"] = [](int x, int y, int w, int h, sol::object color) {
        IRRender::fillGuiRect(IRMath::ivec2(x, y), IRMath::ivec2(w, h), colorFromLua(color));
    };
}

} // namespace IRScript::detail

#endif /* LUA_RENDER_BINDINGS_H */
