#ifndef COMPONENT_EXAMPLE_HPP
#define COMPONENT_EXAMPLE_HPP

#include <string>

namespace IRComponents {

struct C_Example {
    std::string exampleSentence_;
    C_Example(std::string exampleSentence) : exampleSentence_{exampleSentence} {}

    // Default
    C_Example() : exampleSentence_{"This is an example component."} {}
};

} // namespace IRComponents

#endif // COMPONENT_EXAMPLE_HPP
