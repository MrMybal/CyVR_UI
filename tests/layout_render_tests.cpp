// SPDX-License-Identifier: MIT
#include "cyvr/gpu.hpp"
#include "cyvr/layout.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cyvr;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Asset path required");
        VulkanContext context;
        context.createStandalone();
        VulkanRenderer renderer(context);
        renderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, [&](const std::string &name) {
            std::ifstream in(std::string(argv[1]) + "/" + name, std::ios::binary);
            if (!in)
                throw std::runtime_error("Missing asset");
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
        });
        auto target = renderer.createTarget(1440, 990);
        for (auto lang : {"en", "fr"}) {
            UiModel model;
            model.presentation = Presentation::Panel2D;
            model.immersiveAvailable = true;
            model.settings.language = lang;
            for (int sample = 0; sample < 6; sample++) {
                model.page = sample == 0 || sample == 5 ? Page::Home
                             : sample == 1              ? Page::Components
                             : sample == 4              ? Page::Diagnostics
                                                        : Page::Settings;
                model.settingsTab = sample == 3 ? 1 : 0;
                model.modalOpen = sample == 5;
                DrawList original;
                model.drawMain(original);
                renderer.draw(target, original);
                auto before = renderer.readback(target);
                auto screen = captureLayoutScreen(model, "sample", "Sample");
                LayoutDocument doc;
                doc.screens.push_back(screen);
                auto loaded = parseLayout(serializeLayout(doc));
                DrawList replay;
                drawLayout(loaded.screens[0], replay);
                renderer.draw(target, replay);
                auto after = renderer.readback(target);
                double error = 0;
                size_t changed = 0;
                for (size_t i = 0; i < before.size(); i++) {
                    int diff = std::abs(int(before[i]) - int(after[i]));
                    error += diff;
                    if (diff > 4)
                        changed++;
                }
                error /= before.size();
                if (error > .3 || changed > before.size() / 1000)
                    throw std::runtime_error(
                        "Captured layout differs from native screen: " + std::to_string(sample) + " / " +
                        lang + " error=" + std::to_string(error) + " changed=" + std::to_string(changed));
            }
        }
        renderer.destroyTarget(target);
        std::cout << "Native and document rendering match across six screens in English and French\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
