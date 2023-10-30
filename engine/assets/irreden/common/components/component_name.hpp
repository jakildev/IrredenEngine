/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_name.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_NAME_H
#define COMPONENT_NAME_H

#include <string>

namespace IRComponents {

    struct C_Name {
        std::string name_;


        C_Name(std::string name)
        :   name_(name)
        {

        }

        // Default
        C_Name()
        :   name_("Default name component.")
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_NAME_H */
