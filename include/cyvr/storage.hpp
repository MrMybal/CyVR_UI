// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
#include <fstream>
#include <sstream>
namespace cyvr {
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
    std::ofstream f(path);
    f << "language " << s.language << "\ndistance " << s.distance << "\nscale " << s.scale << "\ncontrast "
      << s.highContrast << "\nreducedMotion " << s.reducedMotion << "\nhaptics " << s.haptics << "\nhz "
      << s.requestedHz << "\ntheme " << int(s.theme) << "\nbackground " << int(s.background) << "\nhandModel "
      << int(s.handModel) << "\nfollow " << int(s.follow) << "\nopacity " << s.opacity << "\nblur " << s.blur
      << "\nbrightness " << s.brightness << "\nhandScale " << s.handScale << "\nhandPitch " << s.handPitch
      << "\nrays " << s.rays << '\n';
    return bool(f);
}
inline bool exportDiagnostic(const std::string &path, const UiModel &model) {
    std::ofstream f(path);
    f << model.diagnosticJson();
    return bool(f);
}
} // namespace cyvr
