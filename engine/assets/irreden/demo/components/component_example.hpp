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

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

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