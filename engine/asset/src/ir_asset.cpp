#include <irreden/ir_asset.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <string>

namespace IRAsset {

void saveTrixelTextureData(const std::string &name, const std::string &path, ivec2 size,
                           const std::vector<Color> &colors,
                           const std::vector<Distance> &distances) {
    std::string filename = path + name + ".txl";
    FILE *f = fopen(filename.c_str(), "wb");
    fwrite(&size, sizeof(size), 1, f);
    fwrite(colors.data(), sizeof(colors.at(0)), size.x * size.y, f);
    fwrite(distances.data(), sizeof(distances.at(0)), size.x * size.y, f);
    fclose(f);
    IRE_LOG_INFO("Saved trixel data to {}", filename);
}

void loadTrixelTextureData(const std::string &name, const std::string &path, ivec2 &size,
                           std::vector<Color> &colors, std::vector<Distance> &distances) {
    std::string filename = path + name + ".irtxl";
    FILE *f = fopen(filename.c_str(), "rb");
    fread(&size, sizeof(size), 1, f);
    colors.resize(size.x * size.y);
    distances.resize(size.x * size.y);
    fread(colors.data(), sizeof(colors.at(0)), size.x * size.y, f);
    fread(distances.data(), sizeof(distances.at(0)), size.x * size.y, f);
    fclose(f);
    IRE_LOG_INFO("Loaded trixel data from {}", filename);
}

} // namespace IRAsset