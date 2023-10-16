/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_example.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_EXAMPLE_HPP
#define COMPONENT_EXAMPLE_HPP

#include "../math/ir_math.hpp"
#include "../world/ir_constants.hpp"
#include "component_tags_all.hpp"

#include <string>

using namespace IRMath;

namespace IRComponents {

    struct C_Example {
        std::string exampleSentence_;
        C_Example(std::string exampleSentence)
        :   exampleSentence_{exampleSentence}
        {

        }

        // Default
        C_Example()
        :   exampleSentence_{"This is an example component."}
        {

        }

    };

} // namespace IRComponents

#endif // COMPONENT_EXAMPLE_HPP
