#ifndef TOOL_TARGET_H
#define TOOL_TARGET_H

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>

namespace IREngine::detail {

inline bool isToolTargetName(std::string_view name) {
    if (name.empty() || name.front() == '-') {
        return false;
    }
    for (const unsigned char character : name) {
        if (std::isalnum(character) == 0 && character != '_' && character != '.' &&
            character != '+' && character != '-') {
            return false;
        }
    }
    std::string upper;
    upper.reserve(name.size());
    for (const unsigned char character : name) {
        upper.push_back(static_cast<char>(std::toupper(character)));
    }
    static constexpr std::string_view kDevices[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2",
                                                    "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                                    "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                                    "LPT6", "LPT7", "LPT8", "LPT9"};
    return std::find(std::begin(kDevices), std::end(kDevices), upper) == std::end(kDevices);
}

} // namespace IREngine::detail

#endif /* TOOL_TARGET_H */
