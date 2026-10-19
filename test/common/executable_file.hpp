#ifndef TEST_EXECUTABLE_FILE_H
#define TEST_EXECUTABLE_FILE_H

#include <filesystem>
#include <fstream>
#include <string>

namespace IRTest {

inline void writeExecutableFile(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
    output.close();
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace
    );
}

} // namespace IRTest

#endif /* TEST_EXECUTABLE_FILE_H */
