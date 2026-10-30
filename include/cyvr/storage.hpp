// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
namespace cyvr {
// A process killed during the write leaves the previous file intact.
inline bool replaceFile(const std::string &path, const std::string &content) {
    const std::string temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << content;
    file.close();
    std::error_code error;
    if (file)
        std::filesystem::rename(temporary, path, error);
    if (file && !error)
        return true;
    std::filesystem::remove(temporary, error);
    return false;
}
inline Settings loadSettings(const std::string &path) {
    Settings s;
    std::ifstream file(path);
    std::string key;
    float value;
    while (file >> key) {
        if (key == "language") {
            if (!(file >> s.language))
                break;
            continue;
        }
        if (!(file >> value))
            break;
        if (key == "distance")
            s.distance = value;
        else if (key == "scale")
            s.scale = value;
        else if (key == "contrast")
            s.highContrast = value != 0;
        else if (key == "reducedMotion")
            s.reducedMotion = value != 0;
        else if (key == "haptics")
            s.haptics = value != 0;
        else if (key == "hz")
            s.requestedHz = value;
        else if (key == "theme")
            s.theme = Theme(int(value));
        else if (key == "background")
            s.background = Background(int(value));
        else if (key == "virtualBackground")
            s.virtualBackground = Background(int(value));
        else if (key == "handModel")
            s.handModel = HandModel(int(value));
        else if (key == "follow")
            s.follow = FollowMode(int(value));
        else if (key == "opacity")
            s.opacity = value;
        else if (key == "blur")
            s.blur = value;
        else if (key == "brightness")
            s.brightness = value;
        else if (key == "environmentDim")
            s.environmentDim = value;
        else if (key == "handScale")
            s.handScale = value;
        else if (key == "handPitch")
            s.handPitch = value;
        else if (key == "rays")
            s.rays = value != 0;
    }
    s.sanitize();
    return s;
}
inline bool saveSettings(const std::string &path, const Settings &s) {
    std::ostringstream f;
    f << "language " << s.language << "\ndistance " << s.distance << "\nscale " << s.scale << "\ncontrast "
      << s.highContrast << "\nreducedMotion " << s.reducedMotion << "\nhaptics " << s.haptics << "\nhz "
      << s.requestedHz << "\ntheme " << int(s.theme) << "\nbackground " << int(s.background) << "\nhandModel "
      << int(s.handModel) << "\nfollow " << int(s.follow) << "\nopacity " << s.opacity << "\nblur " << s.blur
      << "\nbrightness " << s.brightness << "\nhandScale " << s.handScale << "\nhandPitch " << s.handPitch
      << "\nenvironmentDim " << s.environmentDim << "\nvirtualBackground "
      << int(s.background == Background::Passthrough ? s.virtualBackground : s.background) << "\nrays "
      << s.rays << '\n';
    return replaceFile(path, f.str());
}
inline bool exportDiagnostic(const std::string &path, const UiModel &model) {
    return replaceFile(path, model.diagnosticJson());
}
} // namespace cyvr
