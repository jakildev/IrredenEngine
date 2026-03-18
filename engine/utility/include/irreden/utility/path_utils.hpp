#ifndef IR_UTILITY_PATH_UTILS_H
#define IR_UTILITY_PATH_UTILS_H

#include <string>

namespace IRUtility {

std::string joinPath(
    const std::string &directory,
    const std::string &filename,
    const std::string &extension
);

std::string pathWithExtension(const std::string &path, const std::string &extension);

std::string formatNumberedFilename(
    const std::string &prefix,
    int index,
    int width,
    const std::string &extension
);

} // namespace IRUtility

#endif /* IR_UTILITY_PATH_UTILS_H */
