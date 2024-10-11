#ifndef CONFIG_OPTION_H
#define CONFIG_OPTION_H

#include <lua54/lua.hpp>

#include <string>

namespace IRScript {


    template<IRScript::LuaType Type>
        struct ConfigOption;

        struct IConfigOption {
            virtual ~IConfigOption() = default;

            virtual void loadValue(lua_State *L) = 0;
        };

        template<>
        struct ConfigOption<IRScript::LuaType::NUMBER> {
            std::string name_;
            double value_;

            ConfigOption(
                std::string name,
                double value
            )
            :   name_{name}
            ,   value_{value}
            {

            }

            // Assumes expected value is on the top of the stack
            void loadValue(lua_State *L) {

        };
}

#endif /* CONFIG_OPTION_H */
