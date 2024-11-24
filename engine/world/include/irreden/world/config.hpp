#ifndef CONFIG_H
#define CONFIG_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_render.hpp>

#include <string>

using namespace IRMath;

namespace IREngine {

    // TODO: https://chatgpt.com/c/67034198-ce68-8005-aa2c-d3a3e08d0d02
    // Remove the interface and make this fully compile time.
    class WorldConfig {
    public:
        WorldConfig(const char* luaConfigFile)
        :   m_lua{luaConfigFile}
        ,   m_config{}
        {
            m_config.addEntry(
                "init_window_width",
                std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1920)
            );
            m_config.addEntry(
                "init_window_height",
                std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1080)
            );
            m_config.addEntry(
                "game_resolution_width",
                std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1920)
            );
            m_config.addEntry(
                "game_resolution_height",
                std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1080)
            );
            m_config.addEntry(
                "fit_mode",
                std::make_unique<
                    IRScript::LuaValue<IRScript::ENUM, IRRender::FitMode>
                >(
                    IRRender::FitMode::FIT,
                    [](
                        const std::string& enumString
                    )
                    {
                        if (enumString == "fit") return IRRender::FitMode::FIT;
                        if (enumString == "stretch") return IRRender::FitMode::STRETCH;
                        IR_ASSERT(false, "Invalid enum value for fit_mode");
                        return IRRender::FitMode::UNKNOWN;
                    }
                )
            );
            m_config.addEntry(
                "fullscreen",
                std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
            );
            sol::table configTable = m_lua.getTable("config");
            m_config.parse(configTable);
        }

        IRScript::ILuaValue& operator[](const std::string& key) {
            return m_config[key];
        }



    private:
        IRScript::LuaScript m_lua;
        IRScript::LuaConfig m_config;
    };


};

#endif /* CONFIG_H */
