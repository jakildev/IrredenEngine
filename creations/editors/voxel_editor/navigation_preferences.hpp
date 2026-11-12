#ifndef NAVIGATION_PREFERENCES_H
#define NAVIGATION_PREFERENCES_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>

namespace IRVoxelEditor {

enum class NavigationScrollMode { AUTO, WHEEL, TRACKPAD };

struct NavigationPreferences {
    float homeZoom_ = 4.0f;
    float wheelZoomRate_ = 0.125f;
    float orbitSensitivity_ = 0.04f;
    float dragSensitivity_ = 0.005f;
    float pinchZoomRate_ = 1.0f;
    float panSensitivity_ = 1.0f;
    NavigationScrollMode scrollMode_ = NavigationScrollMode::AUTO;

    bool valid() const {
        const auto positiveRate = [](float value, float maximum) {
            return IRMath::isFinite(value) && value >= 0.0001f && value <= maximum;
        };
        return IRMath::isFinite(homeZoom_) &&
               (homeZoom_ == 0.0f || (homeZoom_ >= IRConstants::kTrixelCanvasZoomMin.x &&
                                      homeZoom_ <= IRConstants::kTrixelCanvasZoomMax.x)) &&
               positiveRate(wheelZoomRate_, 4.0f) && positiveRate(orbitSensitivity_, 4.0f) &&
               positiveRate(dragSensitivity_, 1.0f) && positiveRate(pinchZoomRate_, 16.0f) &&
               positiveRate(panSensitivity_, 100.0f) &&
               (scrollMode_ == NavigationScrollMode::AUTO ||
                scrollMode_ == NavigationScrollMode::WHEEL ||
                scrollMode_ == NavigationScrollMode::TRACKPAD);
    }

    // Missing or malformed files leave the current settings intact. Callers
    // own the path and decide whether a scripted run may persist preferences.
    bool load(const std::filesystem::path &path) {
        if (path.empty())
            return false;
        std::ifstream stream(path);
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "IRREDEN_EDITOR_NAVIGATION" || version != 1)
            return false;
        NavigationPreferences candidate = *this;
        std::string key;
        while (stream >> std::ws && !stream.eof()) {
            if (!(stream >> key))
                return false;
            float *field = nullptr;
            if (key == "home_zoom")
                field = &candidate.homeZoom_;
            else if (key == "wheel_zoom_rate")
                field = &candidate.wheelZoomRate_;
            else if (key == "orbit_sensitivity")
                field = &candidate.orbitSensitivity_;
            else if (key == "drag_sensitivity")
                field = &candidate.dragSensitivity_;
            else if (key == "pinch_zoom_rate")
                field = &candidate.pinchZoomRate_;
            else if (key == "pan_sensitivity")
                field = &candidate.panSensitivity_;
            else if (key == "scroll_mode") {
                int mode = -1;
                if (!(stream >> mode) || mode < static_cast<int>(NavigationScrollMode::AUTO) ||
                    mode > static_cast<int>(NavigationScrollMode::TRACKPAD))
                    return false;
                candidate.scrollMode_ = static_cast<NavigationScrollMode>(mode);
                continue;
            } else
                return false;
            if (!(stream >> *field))
                return false;
        }
        if (stream.bad() || !candidate.valid())
            return false;
        *this = candidate;
        return true;
    }

    bool save(const std::filesystem::path &path) const {
        if (path.empty() || !valid())
            return false;
        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream stream(temporary, std::ios::trunc);
        stream << std::setprecision(std::numeric_limits<float>::max_digits10)
               << "IRREDEN_EDITOR_NAVIGATION 1\n"
               << "home_zoom " << homeZoom_ << '\n'
               << "wheel_zoom_rate " << wheelZoomRate_ << '\n'
               << "orbit_sensitivity " << orbitSensitivity_ << '\n'
               << "drag_sensitivity " << dragSensitivity_ << '\n'
               << "pinch_zoom_rate " << pinchZoomRate_ << '\n'
               << "pan_sensitivity " << panSensitivity_ << '\n'
               << "scroll_mode " << static_cast<int>(scrollMode_) << '\n';
        stream.close();
        if (!stream)
            return false;
        std::filesystem::rename(temporary, path, error);
        return !error;
    }
};

} // namespace IRVoxelEditor

#endif
