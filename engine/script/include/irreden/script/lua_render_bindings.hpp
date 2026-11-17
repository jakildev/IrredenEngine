#ifndef LUA_RENDER_BINDINGS_H
#define LUA_RENDER_BINDINGS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/glitch_subject.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/post_effects.hpp>
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
// framebuffer glitch state, per-entity canvas glitch subjects, the
// framebuffer post effects, and a
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

// `IRRender.setGlitchSubject` keys. A key outside this list raises.
inline constexpr std::array<std::string_view, 9> kGlitchSubjectKeys{
    "weight",
    "tear",
    "hold",
    "split",
    "echo",
    "dissolve",
    "spill",
    "depthNear",
    "depthFar",
};

// A live entity named by a LuaEntity or an integer id; raises otherwise.
inline IREntity::EntityId glitchSubjectEntity(const sol::object &value, const char *function) {
    const auto fail = [function](const char *reason) {
        return sol::error{std::string("IRRender.") + function + ": entity " + reason};
    };
    IREntity::EntityId entity = IREntity::kNullEntity;
    if (value.is<IRScript::LuaEntity>()) {
        entity = value.as<IRScript::LuaEntity>().entity;
    } else if (value.get_type() == sol::type::number) {
        constexpr double kLargestEntityId = 4294967295.0;
        const double number = value.as<double>();
        if (!(number >= 1.0 && number <= kLargestEntityId) || number != IRMath::floor(number)) {
            throw fail("must be a positive integer id");
        }
        entity = static_cast<IREntity::EntityId>(number);
    } else {
        throw fail("must be an entity id or LuaEntity");
    }
    const IREntity::EntityRecord *record = IREntity::getEntityManager().findRecord(entity);
    if (record == nullptr || record->archetypeNode == nullptr) {
        throw fail("is not a live, placed entity");
    }
    return entity;
}

// The full subject a `setGlitchSubject` table describes: omitted keys take
// the component defaults. Raises on the first bad key or value.
inline IRComponents::C_GlitchSubject glitchSubjectFromLua(sol::table settings) {
    for (const auto &entry : settings) {
        const sol::object &key = entry.first;
        if (key.get_type() != sol::type::string || std::find(
                                                       kGlitchSubjectKeys.begin(),
                                                       kGlitchSubjectKeys.end(),
                                                       key.as<std::string_view>()
                                                   ) == kGlitchSubjectKeys.end()) {
            throw sol::error{
                "IRRender.setGlitchSubject: unknown key (expected weight, tear, hold, split, "
                "echo, dissolve, spill, depthNear or depthFar)"
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
                std::string("IRRender.setGlitchSubject: ") + key + " must be a finite number"
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
            throw sol::error{
                std::string("IRRender.setGlitchSubject: ") + key + " must be in [0, 1]"
            };
        }
        field = static_cast<float>(value);
    };
    const auto integer = [&number](const char *key, double lo, double hi, int &field) {
        double value = 0.0;
        if (!number(key, value)) {
            return false;
        }
        if (value != IRMath::floor(value) || value < lo || value > hi) {
            throw sol::error{
                std::string("IRRender.setGlitchSubject: ") + key + " must be an integer in [" +
                std::to_string(static_cast<long long>(lo)) + ", " +
                std::to_string(static_cast<long long>(hi)) + "]"
            };
        }
        field = static_cast<int>(value);
        return true;
    };

    IRComponents::C_GlitchSubject candidate;
    unit("weight", candidate.weight_);
    unit("tear", candidate.tearWeight_);
    unit("hold", candidate.holdWeight_);
    unit("split", candidate.splitWeight_);
    unit("echo", candidate.echoWeight_);
    unit("dissolve", candidate.dissolveWeight_);
    integer(
        "spill",
        0.0,
        static_cast<double>(IRPrefab::Glitch::kSubjectMaxSpill),
        candidate.spill_
    );
    constexpr double kDepthMin = static_cast<double>(std::numeric_limits<int>::min());
    constexpr double kDepthMax = static_cast<double>(std::numeric_limits<int>::max());
    const bool hasNear = integer("depthNear", kDepthMin, kDepthMax, candidate.depthNear_);
    const bool hasFar = integer("depthFar", kDepthMin, kDepthMax, candidate.depthFar_);
    if (hasNear != hasFar) {
        throw sol::error{"IRRender.setGlitchSubject: depthNear and depthFar go together"};
    }
    candidate.depthEnabled_ = hasNear;
    const std::string error = IRPrefab::Glitch::validateSubject(candidate);
    if (!error.empty()) {
        throw sol::error{"IRRender.setGlitchSubject: " + error};
    }
    return candidate;
}

// `IRRender.setPostEffects` keys. A key outside this list raises.
inline constexpr std::array<std::string_view, 16> kPostEffectsKeys{
    "bloom",
    "bloomThreshold",
    "bloomKnee",
    "bloomIntensity",
    "bloomScatter",
    "bloomLevels",
    "bloomBlend",
    "blur",
    "blurRadius",
    "grade",
    "exposure",
    "lift",
    "gamma",
    "gain",
    "saturation",
    "contrast",
};

// Builds the candidate a `setPostEffects` table describes on top of the
// current state. Raises on the first bad key or value; nothing is stored
// here.
inline IRComponents::C_PostEffectsState postEffectsCandidateFromLua(sol::table settings) {
    namespace PostEffects = IRPrefab::PostEffects;
    const auto fail = [](const std::string &reason) {
        return sol::error{"IRRender.setPostEffects: " + reason};
    };
    for (const auto &entry : settings) {
        const sol::object &key = entry.first;
        if (key.get_type() != sol::type::string || std::find(
                                                       kPostEffectsKeys.begin(),
                                                       kPostEffectsKeys.end(),
                                                       key.as<std::string_view>()
                                                   ) == kPostEffectsKeys.end()) {
            throw fail(
                "unknown key (expected bloom, bloomThreshold, bloomKnee, bloomIntensity, "
                "bloomScatter, bloomLevels, bloomBlend, blur, blurRadius, grade, exposure, lift, "
                "gamma, gain, saturation or contrast)"
            );
        }
    }
    const auto range = [](double lo, double hi) {
        const auto bound = [](double value) {
            std::string text = std::to_string(value);
            text.erase(text.find_last_not_of('0') + 1);
            if (text.back() == '.') {
                text.pop_back();
            }
            return text;
        };
        return "[" + bound(lo) + ", " + bound(hi) + "]";
    };
    const auto within =
        [&fail, &range](const std::string &name, const sol::object &value, double lo, double hi) {
            if (value.get_type() != sol::type::number || !IRMath::isFinite(value.as<double>())) {
                throw fail(name + " must be a finite number");
            }
            const double number = value.as<double>();
            if (number < lo || number > hi) {
                throw fail(name + " must be in " + range(lo, hi));
            }
            return number;
        };
    const auto toggle = [&settings, &fail](const char *key, bool &field) {
        const sol::object value = settings[key];
        if (!value.valid()) {
            return;
        }
        if (value.get_type() != sol::type::boolean) {
            throw fail(std::string(key) + " must be a boolean");
        }
        field = value.as<bool>();
    };
    const auto scalar = [&settings, &within](const char *key, double lo, double hi, float &field) {
        const sol::object value = settings[key];
        if (value.valid()) {
            field = static_cast<float>(within(key, value, lo, hi));
        }
    };
    // A number sets all three channels; a table names them as `r`, `g`, `b`
    // or as indices 1..3, and nothing else.
    const auto channels =
        [&settings, &within, &fail](const char *key, double lo, double hi, IRMath::vec3 &field) {
            const sol::object value = settings[key];
            if (!value.valid()) {
                return;
            }
            if (value.get_type() == sol::type::number) {
                field = IRMath::vec3(static_cast<float>(within(key, value, lo, hi)));
                return;
            }
            if (value.get_type() != sol::type::table) {
                throw fail(std::string(key) + " must be a number or an {r, g, b} table");
            }
            const sol::table table = value.as<sol::table>();
            const bool indexed = table[1].valid();
            std::size_t entries = 0;
            for (const auto &entry : table) {
                (void)entry;
                ++entries;
            }
            constexpr std::array<const char *, 3> kNames{"r", "g", "b"};
            IRMath::vec3 parsed(0.0f);
            for (int channel = 0; channel < 3; ++channel) {
                const sol::object component = indexed ? table.get<sol::object>(channel + 1)
                                                      : table.get<sol::object>(kNames[channel]);
                if (!component.valid()) {
                    throw fail(std::string(key) + " must name exactly three channels");
                }
                parsed[channel] = static_cast<float>(
                    within(std::string(key) + "." + kNames[channel], component, lo, hi)
                );
            }
            if (entries != 3) {
                throw fail(std::string(key) + " must name exactly three channels");
            }
            field = parsed;
        };

    IRComponents::C_PostEffectsState candidate = PostEffects::stateOrDefault();
    toggle("bloom", candidate.bloomEnabled_);
    scalar("bloomThreshold", 0.0, 1.0, candidate.bloomThreshold_);
    scalar("bloomKnee", 0.0, 1.0, candidate.bloomKnee_);
    scalar("bloomIntensity", 0.0, PostEffects::kMaxBloomIntensity, candidate.bloomIntensity_);
    scalar("bloomScatter", 0.0, 1.0, candidate.bloomScatter_);
    if (const sol::object levels = settings["bloomLevels"]; levels.valid()) {
        const double value = within("bloomLevels", levels, 1.0, PostEffects::kMaxBloomLevels);
        if (value != IRMath::floor(value)) {
            throw fail("bloomLevels must be an integer");
        }
        candidate.bloomLevels_ = static_cast<int>(value);
    }
    if (const sol::object blend = settings["bloomBlend"]; blend.valid()) {
        const auto member = "bloomBlend must be an IRRender.BloomBlend member "
                            "(IRRender.BloomBlend.ADD or IRRender.BloomBlend.SCREEN)";
        if (blend.get_type() != sol::type::number) {
            throw fail(member);
        }
        const double value = blend.as<double>();
        if (value == static_cast<double>(IRComponents::BloomBlend::ADD)) {
            candidate.bloomBlend_ = IRComponents::BloomBlend::ADD;
        } else if (value == static_cast<double>(IRComponents::BloomBlend::SCREEN)) {
            candidate.bloomBlend_ = IRComponents::BloomBlend::SCREEN;
        } else {
            throw fail(member);
        }
    }
    toggle("blur", candidate.blurEnabled_);
    scalar("blurRadius", 0.0, PostEffects::kMaxBlurRadius, candidate.blurRadius_);
    toggle("grade", candidate.gradeEnabled_);
    scalar(
        "exposure",
        -PostEffects::kMaxExposureStops,
        PostEffects::kMaxExposureStops,
        candidate.exposureStops_
    );
    channels("lift", -1.0, 1.0, candidate.lift_);
    channels("gamma", PostEffects::kMinGamma, PostEffects::kMaxGamma, candidate.gamma_);
    channels("gain", 0.0, PostEffects::kMaxGain, candidate.gain_);
    scalar("saturation", 0.0, PostEffects::kMaxSaturation, candidate.saturation_);
    scalar("contrast", 0.0, PostEffects::kMaxContrast, candidate.contrast_);
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
    // Per-entity canvas glitch. `setGlitchSubject` replaces the whole
    // configuration; adding the mark is a structural change, so call it
    // outside a system tick or from `IREntity.deferredCall`.
    lua["IRRender"]["setGlitchSubject"] = [](sol::object entity, sol::object settings) {
        const IREntity::EntityId target = glitchSubjectEntity(entity, "setGlitchSubject");
        if (settings.get_type() != sol::type::table) {
            throw sol::error{"IRRender.setGlitchSubject: expected a table"};
        }
        IRPrefab::Glitch::setSubject(target, glitchSubjectFromLua(settings.as<sol::table>()));
    };
    lua["IRRender"]["clearGlitchSubject"] = [](sol::object entity) {
        IRPrefab::Glitch::clearSubject(glitchSubjectEntity(entity, "clearGlitchSubject"));
    };
    lua["IRRender"]["glitchSubject"] = [](sol::object entity,
                                          sol::this_state state) -> sol::object {
        const IRComponents::C_GlitchSubject *subject =
            IRPrefab::Glitch::subjectOrNull(glitchSubjectEntity(entity, "glitchSubject"));
        sol::state_view view{state};
        if (subject == nullptr) {
            return sol::make_object(view, sol::lua_nil);
        }
        sol::table out = view.create_table();
        out["weight"] = subject->weight_;
        out["tear"] = subject->tearWeight_;
        out["hold"] = subject->holdWeight_;
        out["split"] = subject->splitWeight_;
        out["echo"] = subject->echoWeight_;
        out["dissolve"] = subject->dissolveWeight_;
        out["spill"] = subject->spill_;
        if (subject->depthEnabled_) {
            out["depthNear"] = subject->depthNear_;
            out["depthFar"] = subject->depthFar_;
        }
        return out;
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

    // Framebuffer post effects (bloom, blur, colour grade). `setPostEffects`
    // is a partial update: omitted keys keep their values, and every key is
    // checked before any field changes. `postEffects()` returns a fresh table
    // of every key.
    sol::table bloomBlend = lua.create_table();
#define IR_BIND_BLOOM_BLEND(name)                                                                  \
    bloomBlend[#name] = static_cast<lua_Integer>(IRComponents::BloomBlend::name)
    IR_BIND_BLOOM_BLEND(ADD);
    IR_BIND_BLOOM_BLEND(SCREEN);
#undef IR_BIND_BLOOM_BLEND
    lua["IRRender"]["BloomBlend"] = bloomBlend;
    lua["IRRender"]["setPostEffects"] = [](sol::object settings) {
        if (settings.get_type() != sol::type::table) {
            throw sol::error{"IRRender.setPostEffects: expected a table"};
        }
        const IRComponents::C_PostEffectsState candidate =
            postEffectsCandidateFromLua(settings.as<sol::table>());
        const std::string error = IRPrefab::PostEffects::validate(candidate);
        if (!error.empty()) {
            throw sol::error{"IRRender.setPostEffects: " + error};
        }
        IRPrefab::PostEffects::setState(candidate);
    };
    lua["IRRender"]["postEffects"] = [](sol::this_state state) {
        sol::state_view view{state};
        const IRComponents::C_PostEffectsState current = IRPrefab::PostEffects::stateOrDefault();
        const auto channels = [&view](IRMath::vec3 value) {
            sol::table out = view.create_table();
            out["r"] = value.r;
            out["g"] = value.g;
            out["b"] = value.b;
            return out;
        };
        sol::table out = view.create_table();
        out["bloom"] = current.bloomEnabled_;
        out["bloomThreshold"] = current.bloomThreshold_;
        out["bloomKnee"] = current.bloomKnee_;
        out["bloomIntensity"] = current.bloomIntensity_;
        out["bloomScatter"] = current.bloomScatter_;
        out["bloomLevels"] = current.bloomLevels_;
        out["bloomBlend"] = static_cast<lua_Integer>(current.bloomBlend_);
        out["blur"] = current.blurEnabled_;
        out["blurRadius"] = current.blurRadius_;
        out["grade"] = current.gradeEnabled_;
        out["exposure"] = current.exposureStops_;
        out["lift"] = channels(current.lift_);
        out["gamma"] = channels(current.gamma_);
        out["gain"] = channels(current.gain_);
        out["saturation"] = current.saturation_;
        out["contrast"] = current.contrast_;
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
