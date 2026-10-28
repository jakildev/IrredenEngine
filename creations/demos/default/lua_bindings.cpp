#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/script/lua_bindings_default.hpp>
#include <irreden/script/lua_text_bindings.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

namespace IRDefaultCreation {
void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) {
        return;
    }

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;
        using namespace IRConstants;
        using namespace IRAudio;

        IRScript::registerStandardBindings(luaScript);

        registerLuaComponentPack(luaScript);
        luaScript.bindLuaText();
        luaScript.lua()["IRText"]["create"] =
            [](const std::string &text, int x, int y, sol::table opts, sol::this_state L) {
                Color color = IRMath::IRColors::kWhite;
                int wrapWidth = 0;
                int lifetime = 0;
                auto alignH = TextAlignH::LEFT;
                auto alignV = TextAlignV::TOP;
                int fontSize = 2;
                int boxWidth = 0;
                int boxHeight = 0;
                IRRender::TextSize textSize = IRRender::TextSize::LARGE;
                if (opts.valid()) {
                    sol::optional<sol::table> colorOpt = opts["color"];
                    if (colorOpt) {
                        sol::table ct = *colorOpt;
                        color = Color(
                            ct.get_or(1, 255),
                            ct.get_or(2, 255),
                            ct.get_or(3, 255),
                            ct.get_or(4, 255)
                        );
                    }
                    wrapWidth = opts.get_or("wrapWidth", 0);
                    lifetime = opts.get_or("lifetime", 0);
                    alignH = static_cast<TextAlignH>(opts.get_or("alignH", 0));
                    alignV = static_cast<TextAlignV>(opts.get_or("alignV", 0));
                    fontSize = opts.get_or("fontSize", 2);
                    boxWidth = opts.get_or("boxWidth", 0);
                    boxHeight = opts.get_or("boxHeight", 0);
                    const sol::object sizeOpt = opts["size"];
                    textSize = IRScript::textSizeFromLua(sizeOpt, "IRText.create");
                }
                C_TextStyle style{
                    color,
                    wrapWidth,
                    alignH,
                    alignV,
                    boxWidth,
                    boxHeight,
                    fontSize,
                    textSize
                };
                IREntity::EntityId entity;
                if (lifetime > 0) {
                    entity = IREntity::createEntity(
                        C_TextSegment{text},
                        C_GuiPosition{x, y},
                        C_GuiElement{},
                        style,
                        C_Lifetime{lifetime}
                    );
                } else {
                    entity = IREntity::createEntity(
                        C_TextSegment{text},
                        C_GuiPosition{x, y},
                        C_GuiElement{},
                        style
                    );
                }
                return IRScript::LuaEntity{entity};
            };
        luaScript.lua()["IRText"]["setText"] = [](IRScript::LuaEntity handle,
                                                  const std::string &text) {
            IREntity::getComponent<C_TextSegment>(handle.entity).text_ = text;
        };
        luaScript.lua()["IRText"]["remove"] = [](IRScript::LuaEntity handle) {
            IREntity::destroyEntity(handle.entity);
        };

        luaScript.lua()["IREntity"] = luaScript.lua().create_table();
        luaScript.lua()["IREntity"]["destroyEntity"] = [](IRScript::LuaEntity handle) {
            IREntity::destroyEntity(handle.entity);
        };
        luaScript.registerType<
            IREntity::CreateEntityCallbackParams,
            IREntity::CreateEntityCallbackParams(ivec3, vec3)>(
            "CreateEntityCallbackParams",
            "center",
            &IREntity::CreateEntityCallbackParams::center,
            "index",
            &IREntity::CreateEntityCallbackParams::index
        );

        // Batch creation functions — every bob-eligible entity also
        // carries C_Modifiers so PERIODIC_IDLE_POSITION_OFFSET can push a
        // per-frame vec3 modifier into TRANSFORM_TRANSLATION. The modifier
        // folds into C_WorldTransform.translation_; UPDATE_VOXEL_SET_CHILDREN
        // reads that translation as the voxel-set parent position, so the
        // bob reconnects directly through the SQT chain.
        luaScript.registerCreateEntityBatchFunction<
            C_LocalTransform,
            C_VoxelSetNew,
            C_PeriodicIdle,
            C_Modifiers>("createEntityBatchVoxelPeriodicIdle");
        luaScript.registerCreateEntityBatchFunction<
            C_LocalTransform,
            C_VoxelSetNew,
            C_PeriodicIdle,
            C_MidiNote,
            C_Modifiers>("createEntityBatchPolyrhythm");

        // Direct entity creation for MIDI sequences
        luaScript.registerCreateEntityFunction<C_MidiSequence>("createMidiSequence");

        // Render API
        luaScript.lua()["IRRender"] = luaScript.lua().create_table();
        luaScript.lua()["IRRender"]["setGuiScale"] = [](int scale) {
            IRRender::setGuiScale(scale);
        };
        luaScript.lua()["IRRender"]["getGuiScale"] = []() { return IRRender::getGuiScale(); };
        luaScript.lua()["IRRender"]["getMainCanvasSize"] = [](sol::this_state L) {
            vec2 size = IRRender::getMainCanvasSizeTrixels();
            sol::table t = sol::state_view(L).create_table();
            t[1] = static_cast<int>(size.x);
            t[2] = static_cast<int>(size.y);
            return t;
        };
        luaScript.lua()["IRRender"]["getGuiCanvasSize"] = [](sol::this_state L) {
            vec2 mainSize = IRRender::getMainCanvasSizeTrixels();
            int scale = IRRender::getGuiScale();
            sol::table t = sol::state_view(L).create_table();
            t[1] = static_cast<int>(mainSize.x) / scale;
            t[2] = static_cast<int>(mainSize.y) / scale;
            return t;
        };
        luaScript.lua()["IRRender"]["measureText"] =
            [](const std::string &text, sol::optional<int> wrapWidth, sol::this_state L) {
                ivec2 size = IRRender::measureText(text, wrapWidth.value_or(0));
                sol::table t = sol::state_view(L).create_table();
                t[1] = size.x;
                t[2] = size.y;
                return t;
            };
        luaScript.lua()["IRRender"]["glyphWidth"] = IRRender::kGlyphWidth;
        luaScript.lua()["IRRender"]["glyphHeight"] = IRRender::kGlyphHeight;
        luaScript.lua()["IRRender"]["glyphStepX"] = IRRender::kGlyphStepX;
        luaScript.lua()["IRRender"]["glyphStepY"] = IRRender::kGlyphStepY;

        // Audio API
        luaScript.lua()["IRAudio"] = luaScript.lua().create_table();
        luaScript.lua()["IRAudio"]["midiInPorts"] = [](sol::this_state L) {
            sol::state_view lua(L);
            sol::table t = lua.create_table();
            const auto ports = IRAudio::midiInPorts();
            for (int i = 0; i < static_cast<int>(ports.size()); i++) {
                t[i + 1] = ports[i];
            }
            return t;
        };
        luaScript.lua()["IRAudio"]["midiOutPorts"] = [](sol::this_state L) {
            sol::state_view lua(L);
            sol::table t = lua.create_table();
            const auto ports = IRAudio::midiOutPorts();
            for (int i = 0; i < static_cast<int>(ports.size()); i++) {
                t[i + 1] = ports[i];
            }
            return t;
        };
        luaScript.lua()["IRAudio"]["openMidiIn"] = [](const std::string &name,
                                                      sol::this_state L) -> sol::variadic_results {
            sol::variadic_results results;
            int port = IRAudio::openPortMidiIn(name);
            if (port < 0) {
                results.push_back({L, sol::in_place, sol::lua_nil});
                results.push_back({L, sol::in_place, "no MIDI input port matching: " + name});
            } else {
                results.push_back({L, sol::in_place, port});
            }
            return results;
        };
        luaScript.lua()["IRAudio"]["openMidiOut"] = [](const std::string &name,
                                                       sol::this_state L) -> sol::variadic_results {
            sol::variadic_results results;
            int port = IRAudio::openPortMidiOut(name);
            if (port < 0) {
                results.push_back({L, sol::in_place, sol::lua_nil});
                results.push_back({L, sol::in_place, "no MIDI output port matching: " + name});
            } else {
                results.push_back({L, sol::in_place, port});
            }
            return results;
        };

        // Expose IRCommand.{bindPrefab, createCommand, fire, fireByName}
        // + the IRInput enum tables so scripts/commands.lua can declare
        // the demo's input bindings.
        luaScript.bindLuaCommands();
    });

    isRegistered = true;
}
} // namespace IRDefaultCreation
