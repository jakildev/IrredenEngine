#include "../../creations/editors/voxel_editor/navigation_preferences.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

using IRVoxelEditor::NavigationPreferences;
using IRVoxelEditor::NavigationScrollMode;

class VoxelEditorNavigationPreferences : public testing::Test {
  protected:
    void SetUp() override {
        m_directory = std::filesystem::temp_directory_path() /
                      ("ir-navigation-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        m_path = m_directory / "preferences" / "navigation.cfg";
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }
    void write(const std::string &text) {
        std::filesystem::create_directories(m_path.parent_path());
        std::ofstream stream(m_path);
        stream << text;
    }
    std::filesystem::path m_directory;
    std::filesystem::path m_path;
};

TEST_F(VoxelEditorNavigationPreferences, DefaultsAndMissingFileHaveNoFilesystemSideEffects) {
    NavigationPreferences preferences;
    EXPECT_TRUE(preferences.valid());
    EXPECT_FLOAT_EQ(preferences.homeZoom_, 4.0f);
    EXPECT_FLOAT_EQ(preferences.wheelZoomRate_, 0.125f);
    EXPECT_FLOAT_EQ(preferences.orbitSensitivity_, 0.04f);
    EXPECT_FLOAT_EQ(preferences.dragSensitivity_, 0.005f);
    EXPECT_FLOAT_EQ(preferences.pinchZoomRate_, 1.0f);
    EXPECT_FLOAT_EQ(preferences.panSensitivity_, 1.0f);
    EXPECT_EQ(preferences.scrollMode_, NavigationScrollMode::AUTO);
    EXPECT_FALSE(preferences.load(m_path));
    EXPECT_FALSE(preferences.save({}));
    EXPECT_FALSE(std::filesystem::exists(m_directory));
}

TEST_F(VoxelEditorNavigationPreferences, RoundTripPreservesEverySettingAndReplacesPreviousFile) {
    NavigationPreferences preferences;
    ASSERT_TRUE(preferences.save(m_path));
    preferences.homeZoom_ = 0.0f;
    preferences.wheelZoomRate_ = 0.3f;
    preferences.orbitSensitivity_ = 0.017f;
    preferences.dragSensitivity_ = 0.0027f;
    preferences.pinchZoomRate_ = 1.7f;
    preferences.panSensitivity_ = 2.5f;
    preferences.scrollMode_ = NavigationScrollMode::TRACKPAD;
    ASSERT_TRUE(preferences.save(m_path));
    NavigationPreferences restored;
    ASSERT_TRUE(restored.load(m_path));
    EXPECT_FLOAT_EQ(restored.homeZoom_, preferences.homeZoom_);
    EXPECT_FLOAT_EQ(restored.wheelZoomRate_, preferences.wheelZoomRate_);
    EXPECT_FLOAT_EQ(restored.orbitSensitivity_, preferences.orbitSensitivity_);
    EXPECT_FLOAT_EQ(restored.dragSensitivity_, preferences.dragSensitivity_);
    EXPECT_FLOAT_EQ(restored.pinchZoomRate_, preferences.pinchZoomRate_);
    EXPECT_FLOAT_EQ(restored.panSensitivity_, preferences.panSensitivity_);
    EXPECT_EQ(restored.scrollMode_, preferences.scrollMode_);
    EXPECT_FALSE(std::filesystem::exists(m_path.string() + ".tmp"));
}

TEST_F(VoxelEditorNavigationPreferences, PartialConfigKeepsUnspecifiedValues) {
    write("IRREDEN_EDITOR_NAVIGATION 1\nscroll_mode 1\nhome_zoom 8\n");
    NavigationPreferences preferences;
    preferences.panSensitivity_ = 2.0f;
    ASSERT_TRUE(preferences.load(m_path));
    EXPECT_FLOAT_EQ(preferences.homeZoom_, 8.0f);
    EXPECT_FLOAT_EQ(preferences.panSensitivity_, 2.0f);
    EXPECT_EQ(preferences.scrollMode_, NavigationScrollMode::WHEEL);
}

TEST_F(VoxelEditorNavigationPreferences, InvalidConfigDoesNotPartiallyApply) {
    for (const char *invalid :
         {"home_zoom 0.5",
          "home_zoom 65",
          "home_zoom nan",
          "wheel_zoom_rate 0",
          "orbit_sensitivity -1",
          "drag_sensitivity 2",
          "pinch_zoom_rate 17",
          "pan_sensitivity 101",
          "scroll_mode 3",
          "scroll_mode 0.5",
          "home_zoom",
          "unknown 1"}) {
        write(std::string("IRREDEN_EDITOR_NAVIGATION 1\nhome_zoom 8\n") + invalid + "\n");
        NavigationPreferences preferences;
        EXPECT_FALSE(preferences.load(m_path)) << invalid;
        EXPECT_FLOAT_EQ(preferences.homeZoom_, 4.0f) << invalid;
    }
    write("IRREDEN_EDITOR_NAVIGATION 2\nhome_zoom 8\n");
    NavigationPreferences preferences;
    EXPECT_FALSE(preferences.load(m_path));
}

TEST_F(VoxelEditorNavigationPreferences, InvalidSavePreservesExistingFile) {
    NavigationPreferences preferences;
    ASSERT_TRUE(preferences.save(m_path));
    preferences.homeZoom_ = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(preferences.save(m_path));
    preferences.homeZoom_ = 4.0f;
    preferences.orbitSensitivity_ = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(preferences.save(m_path));
    NavigationPreferences restored;
    ASSERT_TRUE(restored.load(m_path));
    EXPECT_FLOAT_EQ(restored.homeZoom_, 4.0f);
    EXPECT_FLOAT_EQ(restored.orbitSensitivity_, 0.04f);
}

} // namespace
