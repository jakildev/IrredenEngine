#include <gtest/gtest.h>

#include <irreden/world/config.hpp>

#include <filesystem>
#include <fstream>

TEST(WorldConfigMetalOverlap, PresetRestoresBlockingPresentWithoutChangingOtherFields) {
    const auto directory = std::filesystem::temp_directory_path();
    const auto source = directory / "ir_metal_overlap_config.lua";
    const auto preset = directory / "ir_metal_overlap_preset.lua";
    {
        std::ofstream out{source};
        out << "config = { init_window_width = 640 }\n";
    }
    {
        std::ofstream out{preset};
        out << "config = { metal_frame_overlap = false }\n";
    }
    IREngine::WorldConfig overlap{source.string().c_str()};
    EXPECT_TRUE(overlap["metal_frame_overlap"].get_boolean());
    IREngine::WorldConfig blocking{source.string().c_str(), preset.string().c_str()};
    EXPECT_FALSE(blocking["metal_frame_overlap"].get_boolean());
    EXPECT_EQ(blocking["init_window_width"].get_integer(), 640);
    std::filesystem::remove(source);
    std::filesystem::remove(preset);
}
