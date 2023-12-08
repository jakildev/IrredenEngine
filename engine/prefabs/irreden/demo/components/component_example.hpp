/*
 * Project: Irreden Engine
 * File: component_example.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_EXAMPLE_HPP
#define COMPONENT_EXAMPLE_HPP

#include <string>

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
