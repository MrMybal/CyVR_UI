// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <windowsx.h>
#include <psapi.h>
// clang-format on
#include "cyvr/gpu.hpp"
#include "cyvr/storage.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace cyvr;
namespace {
constexpr int canvasHeight = 1120;
struct Desktop {
    VulkanContext context;
    VulkanRenderer renderer{context};
    Target target;
    UiModel model;
    DrawList draw;
    HandSurface controllerSurface;
    std::vector<uint8_t> bitmap;
    fs::path directory;
    uint64_t rendered{};
    HWND window{};
    bool pressed{};
    bool demoPreview{};
    double telemetryTime{};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Desktop() {
        if (target.image)
            renderer.destroyTarget(target);
    }
    void initialize(const fs::path &assets) {
        context.createStandalone();
        renderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, [&](const std::string &name) {
            std::ifstream f(assets / name, std::ios::binary);
            if (!f)
                throw std::runtime_error("Missing asset: " + name);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
        });
        target = renderer.createTarget(1440, canvasHeight);
        std::ifstream handFile(assets / "hand_studio.bin", std::ios::binary);
        controllerSurface =
            HandSurface::load(std::vector<uint8_t>((std::istreambuf_iterator<char>(handFile)), {}));
        model.settings = loadSettings((directory / "preferences.txt").string());
        model.log("CyVR UI / native Windows reference");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(context.physical, &props);
        model.log(std::string("Vulkan: ") + props.deviceName);
    }
    void render() {
        if (rendered == model.revision)
            return;
        auto before = std::chrono::steady_clock::now();
        PROCESS_MEMORY_COUNTERS memory{};
        if (!demoPreview && GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
            model.stats.rssMiB = memory.WorkingSetSize / 1048576.;
        model.drawMain(draw);
        if (model.windowState != WindowState::Open)
            draw.clear();
        DrawList dock;
        model.drawDock(dock);
        for (auto v : dock.vertices) {
            v.x += dockOriginX;
            v.y += dockOriginY;
            draw.vertices.push_back(v);
        }
        if (model.debugVisible && model.windowState == WindowState::Open) {
            DrawList debug;
            model.drawDebug(debug);
            for (auto v : debug.vertices) {
                v.x = 1020 + v.x * .62f;
                v.y = 96 + v.y * .62f;
                draw.vertices.push_back(v);
            }
        }
        PanelGeometry p = placePanel({{0, 1.6f, 0}, {}}, model.settings.distance, model.settings.scale);
        EnvironmentView glass = glassView({{0, 1.6f, 0}, {}}, p, model.settings);
        glass.shape = {1440.f / canvasHeight, 30.f / canvasHeight, 1, 0};
        renderer.draw(target, draw, &glass);
        bitmap = renderer.readback(target);
        for (size_t i = 0; i < bitmap.size(); i += 4)
            std::swap(bitmap[i], bitmap[i + 2]);
        if (!demoPreview)
            model.stats.cpuMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        rendered = model.revision;
    }
    void spatialPreview() {
        renderer.destroyTarget(target);
        target = renderer.createTarget(1440, canvasHeight, VK_NULL_HANDLE, true);
        SpatialMesh mesh;
        mesh.controllerHand({{-.13f, -.03f, -.40f}, yawRotation(-.35f)}, true, .2f, model.settings.handModel,
                            model.settings.handScale, model.settings.handPitch, &controllerSurface);
        mesh.controllerHand({{.13f, -.03f, -.40f}, yawRotation(.35f)}, false, .7f, model.settings.handModel,
                            model.settings.handScale, model.settings.handPitch, &controllerSurface);
        mesh.bone({-.19f, -.09f, -.55f}, {-.08f, .1f, -1.35f}, .0012f, Color::hex(0x86e7cf), 6);
        mesh.bone({.19f, -.09f, -.55f}, {.08f, .1f, -1.35f}, .0012f, Color::hex(0x86e7cf), 6);
        auto env = environmentView({{0, 1.6f, 0}, {}}, -.7f, .7f, .52f, -.52f, model.settings);
        renderer.drawSpatial(target, mesh, viewProjection({{}, {}}, -.7f, .7f, .52f, -.52f), &env);
        bitmap = renderer.readback(target);
        for (size_t i = 0; i < bitmap.size(); i += 4)
            std::swap(bitmap[i], bitmap[i + 2]);
    }
    Vec2 point(LPARAM param) const {
        RECT rect{};
        GetClientRect(window, &rect);
        float scale = std::min((rect.right - 24) / 1440.f, (rect.bottom - 24) / float(canvasHeight));
        if (scale <= 0)
            return {-100, -100};
        float left = (rect.right - 1440 * scale) / 2, top = (rect.bottom - canvasHeight * scale) / 2;
        return {(GET_X_LPARAM(param) - left) / scale, (GET_Y_LPARAM(param) - top) / scale};
    }
    void move(LPARAM param, bool down) {
        Vec2 p = point(param);
        if (model.debugVisible && Rect{1020, 96, 384.4f, 471.2f}.contains(p))
            p = {-100, -100};
        model.pointer(0, p, down);
        InvalidateRect(window, nullptr, FALSE);
    }
    void tick() {
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        model.tick(now);
        if (now - telemetryTime > .5 && (model.page == Page::Diagnostics || model.debugVisible)) {
            telemetryTime = now;
            model.invalidate();
        }
        if (model.recenterRequested) {
            model.recenterRequested = false;
            model.log("Anchor recentered / desktop preview");
        }
        if (model.saveRequested) {
            model.saveRequested = false;
            if (!saveSettings((directory / "preferences.txt").string(), model.settings))
                model.notify("toast.save_failed_desktop");
        }
        if (model.exportRequested) {
            model.exportRequested = false;
            std::string path = (directory / "diagnostic.json").string();
            if (exportDiagnostic(path, model))
                model.notify("toast.exported_desktop");
            else
                model.notify("toast.export_failed_desktop");
        }
        if (rendered != model.revision)
            InvalidateRect(window, nullptr, FALSE);
    }
};
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *app = reinterpret_cast<Desktop *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Desktop *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, w, l);
    try {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            app->render();
            RECT r{};
            GetClientRect(window, &r);
            FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            float scale =
                std::max(.01f, std::min((r.right - 24) / 1440.f, (r.bottom - 24) / float(canvasHeight)));
            int ww = int(1440 * scale), hh = int(canvasHeight * scale);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 1440;
            info.bmiHeader.biHeight = -canvasHeight;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, (r.right - ww) / 2, (r.bottom - hh) / 2, ww, hh, 0, 0, 1440, canvasHeight,
                          app->bitmap.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_MOUSEMOVE:
            app->move(l, app->pressed);
            return 0;
        case WM_LBUTTONDOWN:
            app->pressed = true;
            SetCapture(window);
            app->move(l, true);
            if (app->model.dragPointer() == 0) {
                app->model.pointer(0, {-100, -100}, false);
                app->pressed = false;
                ReleaseCapture();
                SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        case WM_LBUTTONUP:
            app->pressed = false;
            ReleaseCapture();
            app->move(l, false);
            return 0;
        case WM_CAPTURECHANGED:
            if (app->pressed) {
                app->pressed = false;
                app->model.pointer(0, {-100, -100}, false);
            }
            return 0;
        case WM_SIZE:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            app->tick();
            return 0;
        case WM_KEYDOWN:
            if (w >= '1' && w <= '4')
                app->model.navigate(Page(w - '1'));
            if (w == 'D') {
                app->model.debugVisible = !app->model.debugVisible;
                app->model.invalidate();
            }
            if (w == 'R')
                app->model.recenterRequested = true;
            if (w == VK_ESCAPE) {
                app->model.modalOpen = false;
                app->model.invalidate();
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, w, l);
        }
    } catch (const std::exception &error) {
        MessageBoxA(window, error.what(), "CyVR UI / Vulkan", MB_ICONERROR);
        PostQuitMessage(1);
        return 0;
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        SetProcessDPIAware();
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        fs::path dir = fs::path(executable).parent_path();
        fs::path assets = dir / "assets", snapshot;
        Page page = Page::Home;
        std::string language;
        int settingsTab = 0, theme = -1, handModel = -1, windowState = 0, hoverId = 0;
        bool spatialPreview = false;
        for (int i = 1; i < argc; i++) {
            std::string argument = argv[i];
            if (argument == "--assets" && i + 1 < argc)
                assets = argv[++i];
            else if (argument == "--snapshot" && i + 1 < argc)
                snapshot = argv[++i];
            else if (argument == "--language" && i + 1 < argc)
                language = argv[++i];
            else if (argument == "--theme" && i + 1 < argc)
                theme = std::stoi(argv[++i]);
            else if (argument == "--tab" && i + 1 < argc)
                settingsTab = std::stoi(argv[++i]);
            else if (argument == "--hand-model" && i + 1 < argc)
                handModel = std::stoi(argv[++i]);
            else if (argument == "--window-state" && i + 1 < argc)
                windowState = std::stoi(argv[++i]);
            else if (argument == "--hover-id" && i + 1 < argc)
                hoverId = std::stoi(argv[++i]);
            else if (argument == "--page" && i + 1 < argc) {
                std::string p = argv[++i];
                spatialPreview = p == "spatial";
                page = p == "components" ? Page::Components
                       : p == "settings" ? Page::Settings
                       : p == "debug"    ? Page::Diagnostics
                                         : Page::Home;
            }
        }
        Desktop app;
        app.directory = dir;
        app.initialize(assets);
        if (!snapshot.empty()) {
            app.model.settings = Settings{};
            app.demoPreview = true;
            app.model.runtime = "Demo session / OpenXR";
            app.model.logs = {"Reference catalogue ready", "Demo session started", "Pointer connected"};
            for (int i = 0; i < 120; i++)
                app.model.stats.sample(1000. / 72);
            app.model.stats.cpuMs = 2;
            app.model.stats.displayHz = 72;
            app.model.stats.rssMiB = 128;
        }
        if (!language.empty())
            app.model.settings.language = resolveLocale(language).code;
        if (theme >= 0)
            app.model.settings.theme = Theme(theme);
        if (handModel >= 0)
            app.model.settings.handModel = HandModel(handModel);
        app.model.settings.sanitize();
        app.model.settingsTab = std::clamp(settingsTab, 0, 2);
        app.model.windowState = WindowState(std::clamp(windowState, 0, 2));
        app.model.navigate(page);
        if (hoverId) {
            DrawList sample;
            app.model.drawMain(sample);
            app.model.drawDock(sample);
            for (auto &w : app.model.widgets())
                if (w.id == hoverId) {
                    app.model.pointer(0, {w.rect.x + w.rect.w * .5f, w.rect.y + w.rect.h * .5f}, false);
                    break;
                }
        }
        if (!snapshot.empty()) {
            if (spatialPreview)
                app.spatialPreview();
            else
                app.render();
            std::ofstream out(snapshot, std::ios::binary);
            out << "P6\n1440 " << canvasHeight << "\n255\n";
            for (size_t i = 0; i < app.bitmap.size(); i += 4) {
                out.put(char(app.bitmap[i + 2]));
                out.put(char(app.bitmap[i + 1]));
                out.put(char(app.bitmap[i]));
            }
            if (!out)
                throw std::runtime_error("Cannot write snapshot");
            std::cout << "Vulkan snapshot: " << snapshot.string() << '\n';
            return 0;
        }
        FreeConsole();
        WNDCLASSW cls{};
        cls.lpfnWndProc = windowProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"CyVR.NativeReference";
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&cls);
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"CyVR UI — Native C++ / Vulkan reference",
                                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1300, 940, nullptr,
                                      nullptr, cls.hInstance, &app);
        if (!window)
            throw std::runtime_error("Window creation failed");
        SetTimer(window, 1, 100, nullptr);
        ShowWindow(window, SW_SHOW);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return int(message.wParam);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
