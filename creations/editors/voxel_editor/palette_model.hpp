#ifndef PALETTE_MODEL_H
#define PALETTE_MODEL_H

#include "composite_save_transaction.hpp"
#include "palette.hpp"

#include <charconv>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace IRVoxelEditor {

inline constexpr int kPaletteCapacity = 256;

struct PaletteEntry {
    IRMath::Color color_;
    std::string name_;
};

struct PaletteResult {
    bool ok_ = false;
    std::string error_;
};

inline std::optional<IRMath::Color> parsePaletteHex(std::string_view text) {
    if (text.starts_with('#'))
        text.remove_prefix(1);
    if (text.size() != 6)
        return std::nullopt;
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return IRMath::Color{
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value),
        255
    };
}

inline std::string paletteHex(IRMath::Color color) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string result = "#000000";
    const std::array channels{color.red_, color.green_, color.blue_};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        result[1 + i * 2] = digits[channels[i] >> 4];
        result[2 + i * 2] = digits[channels[i] & 15];
    }
    return result;
}

// Swatches supply RGB values for future strokes; authored voxels own their
// colour directly and have no references into this palette.
class EditorPalette {
  public:
    EditorPalette() {
        resetDefaults();
    }

    int size() const {
        return static_cast<int>(m_entries.size());
    }

    std::span<const PaletteEntry> entries() const {
        return m_entries;
    }

    const std::string &name() const {
        return m_name;
    }

    IRMath::Color color(int index) const {
        return m_entries[static_cast<std::size_t>(IRMath::clamp(index, 0, size() - 1))].color_;
    }

    bool edit(int index, IRMath::Color color) {
        if (index < 0 || index >= size())
            return false;
        color.alpha_ = 255;
        m_entries[static_cast<std::size_t>(index)].color_ = color;
        return true;
    }

    bool add(IRMath::Color color) {
        if (size() >= kPaletteCapacity)
            return false;
        color.alpha_ = 255;
        m_entries.push_back({color, {}});
        return true;
    }

    void resetDefaults() {
        m_entries.clear();
        for (int index = 0; index < kPaletteCount; ++index)
            m_entries.push_back({kPaletteColors[index], std::string(kPaletteNames[index])});
        m_name = "Default";
    }

    PaletteResult loadGpl(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            return {false, "Could not open palette: " + path.string()};
        constexpr std::streamoff maxBytes = 1024 * 1024;
        const auto bytes = input.tellg();
        if (bytes < 0 || bytes > maxBytes)
            return {false, "Palette must be at most 1 MiB"};
        std::string text(static_cast<std::size_t>(bytes), '\0');
        input.seekg(0);
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!input)
            return {false, "Could not read palette: " + path.string()};
        return loadGplText(text, path.stem().string());
    }

    PaletteResult loadGplText(std::string_view text, std::string fallbackName = "Palette") {
        if (text.size() > 1024 * 1024)
            return {false, "Palette must be at most 1 MiB"};
        if (text.find('\0') != std::string_view::npos)
            return {false, "Palette contains a NUL byte"};
        std::istringstream input{std::string(text)};
        std::string line;
        const auto readLine = [&]() {
            if (!std::getline(input, line))
                return false;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        };
        if (!readLine() || line != "GIMP Palette")
            return {false, "Expected a GIMP Palette (.gpl) file"};
        std::vector<PaletteEntry> entries;
        std::string name = std::move(fallbackName);
        int lineNumber = 1;
        bool named = false;
        while (readLine()) {
            ++lineNumber;
            if (lineNumber == 2 && line.starts_with("Name: ")) {
                name = line.substr(6);
                named = true;
                continue;
            }
            if (lineNumber == 3 && named && line.starts_with("Columns: ")) {
                const std::string_view value(line.data() + 9, line.size() - 9);
                unsigned columns = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), columns);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    columns > 255)
                    return {false, "Invalid palette column count"};
                continue;
            }
            if (line.empty() || line.front() == '#' ||
                line.find_first_not_of(" \t") == std::string::npos)
                continue;
            std::istringstream row(line);
            int red = -1, green = -1, blue = -1;
            if (!(row >> red >> green >> blue) || red < 0 || red > 255 || green < 0 ||
                green > 255 || blue < 0 || blue > 255)
                return {false, "Invalid RGB values on palette line " + std::to_string(lineNumber)};
            if (row.peek() != std::char_traits<char>::eof() && row.peek() != ' ' &&
                row.peek() != '\t')
                return {
                    false,
                    "Missing color-name separator on palette line " + std::to_string(lineNumber)
                };
            std::string label;
            std::getline(row >> std::ws, label);
            if (entries.size() >= static_cast<std::size_t>(kPaletteCapacity))
                return {false, "Palette supports at most 256 colors"};
            entries.push_back(
                {IRMath::Color{
                     static_cast<std::uint8_t>(red),
                     static_cast<std::uint8_t>(green),
                     static_cast<std::uint8_t>(blue),
                     255
                 },
                 std::move(label)}
            );
        }
        if (entries.empty())
            return {false, "Palette contains no colors"};
        m_entries = std::move(entries);
        m_name = name.empty() ? "Palette" : std::move(name);
        return {true, {}};
    }

    PaletteResult saveGpl(const std::filesystem::path &path) const {
        if (path.empty() || path.filename().empty())
            return {false, "Enter a palette filename ending in .gpl"};
        const auto directory =
            path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return {false, "Could not create palette directory: " + error.message()};
        CompositeSaveTransaction transaction;
        const auto filename = path.filename().string();
        if (!transaction.begin(directory, filename))
            return {false, transaction.error()};
        const auto staged = transaction.stageFile(filename);
        if (!staged)
            return {false, transaction.error()};
        std::ofstream output(*staged, std::ios::binary | std::ios::trunc);
        if (!output)
            return {false, "Could not stage palette file"};
        output << "GIMP Palette\nName: " << m_name << "\nColumns: 4\n#\n";
        for (const auto &entry : m_entries)
            output << static_cast<int>(entry.color_.red_) << ' '
                   << static_cast<int>(entry.color_.green_) << ' '
                   << static_cast<int>(entry.color_.blue_) << ' ' << entry.name_ << '\n';
        output.close();
        if (!output)
            return {false, "Could not write palette file"};
        if (!transaction.publish())
            return {false, transaction.error()};
        return {true, {}};
    }

  private:
    std::vector<PaletteEntry> m_entries;
    std::string m_name;
};

} // namespace IRVoxelEditor

#endif
