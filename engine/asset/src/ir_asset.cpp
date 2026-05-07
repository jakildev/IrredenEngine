#include <irreden/ir_asset.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_utility.hpp>

#include <cstdio>
#include <string>

namespace IRAsset {

namespace {
constexpr const char *kTrixelExtension = ".txl";
constexpr const char *kIRSpriteExtension = ".irsprite";
} // namespace

void saveTrixelTextureData(
    const std::string &name,
    const std::string &path,
    ivec2 size,
    const std::vector<Color> &colors,
    const std::vector<Distance> &distances
) {
    const std::string filename = IRUtility::joinPath(path, name, kTrixelExtension);
    FILE *f = fopen(filename.c_str(), "wb");
    if (!f) {
        IRE_LOG_ERROR("Failed to open file for writing: {}", filename);
        return;
    }
    fwrite(&size, sizeof(size), 1, f);
    fwrite(colors.data(), sizeof(colors.at(0)), size.x * size.y, f);
    fwrite(distances.data(), sizeof(distances.at(0)), size.x * size.y, f);
    fclose(f);
    IRE_LOG_INFO("Saved trixel data to {}", filename);
}

void loadTrixelTextureData(
    const std::string &name,
    const std::string &path,
    ivec2 &size,
    std::vector<Color> &colors,
    std::vector<Distance> &distances
) {
    const std::string filename = IRUtility::joinPath(path, name, kTrixelExtension);
    FILE *f = fopen(filename.c_str(), "rb");
    if (!f) {
        IRE_LOG_ERROR("Failed to open file for reading: {}", filename);
        return;
    }
    fread(&size, sizeof(size), 1, f);
    colors.resize(size.x * size.y);
    distances.resize(size.x * size.y);
    fread(colors.data(), sizeof(colors.at(0)), size.x * size.y, f);
    fread(distances.data(), sizeof(distances.at(0)), size.x * size.y, f);
    fclose(f);
    IRE_LOG_INFO("Loaded trixel data from {}", filename);
}

void saveSpriteSheetMeta(
    const std::string &name, const std::string &path, const SpriteSheetMeta &meta
) {
    const std::string filename = IRUtility::joinPath(path, name, kIRSpriteExtension);
    FILE *f = fopen(filename.c_str(), "w");
    if (!f) {
        IRE_LOG_ERROR("Failed to open file for writing: {}", filename);
        return;
    }
    fprintf(f, "cellWidth %u\n", meta.cellSizePx_.x);
    fprintf(f, "cellHeight %u\n", meta.cellSizePx_.y);
    fprintf(f, "margin %d\n", meta.margin_);
    fprintf(f, "padding %d\n", meta.padding_);
    for (const auto &a : meta.animations_) {
        // Loader scans names with `%127s`, which truncates at the first
        // space/tab. Catch it at write time rather than load time.
        IR_ASSERT(
            a.name_.find_first_of(" \t\r\n") == std::string::npos,
            "sprite-sheet animation name '{}' contains whitespace",
            a.name_
        );
        fprintf(f, "anim %s %d %d %g\n", a.name_.c_str(), a.firstFrame_, a.frameCount_, a.fps_);
    }
    fclose(f);
    IRE_LOG_INFO("Saved sprite sheet meta to {}", filename);
}

SpriteSheetMeta loadSpriteSheetMeta(const std::string &name, const std::string &path) {
    const std::string filename = IRUtility::joinPath(path, name, kIRSpriteExtension);
    FILE *f = fopen(filename.c_str(), "r");
    if (!f) {
        IRE_LOG_ERROR("Failed to open file for reading: {}", filename);
        return {};
    }
    SpriteSheetMeta meta{};
    char line[256];
    while (fgets(line, static_cast<int>(sizeof(line)), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        char key[64];
        if (sscanf(line, "%63s", key) != 1)
            continue;
        if (strcmp(key, "cellWidth") == 0) {
            sscanf(line, "cellWidth %u", &meta.cellSizePx_.x);
        } else if (strcmp(key, "cellHeight") == 0) {
            sscanf(line, "cellHeight %u", &meta.cellSizePx_.y);
        } else if (strcmp(key, "margin") == 0) {
            sscanf(line, "margin %d", &meta.margin_);
        } else if (strcmp(key, "padding") == 0) {
            sscanf(line, "padding %d", &meta.padding_);
        } else if (strcmp(key, "anim") == 0) {
            SpriteAnimationDesc anim{};
            char animName[128];
            if (sscanf(
                    line,
                    "anim %127s %d %d %f",
                    animName,
                    &anim.firstFrame_,
                    &anim.frameCount_,
                    &anim.fps_
                ) == 4) {
                anim.name_ = animName;
                meta.animations_.push_back(std::move(anim));
            }
        }
    }
    fclose(f);
    IRE_LOG_INFO("Loaded sprite sheet meta from {}", filename);
    return meta;
}

} // namespace IRAsset
