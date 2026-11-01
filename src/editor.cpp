// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "cyvr/gpu.hpp"
#include "cyvr/layout.hpp"
// clang-format off
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <wincodec.h>
#include <wrl/client.h>
// clang-format on

using namespace cyvr;
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
namespace {
enum {
    FromApp = 100,
    New,
    Open,
    Save,
    SaveAs,
    Export,
    Undo,
    Redo,
    Schematic,
    Snap,
    Fit,
    AddText,
    AddButton,
    AddRect,
    AddIcon,
    Duplicate,
    Delete,
    Front,
    Back,
    AddScreen,
    Screens = 150,
    Layers,
    Language,
    Apply,
    ApplyCanvas,
    Name = 200,
    X,
    Y,
    W,
    H,
    Text,
    Font,
    Radius,
    ColorField,
    Action,
    Key,
    Visible,
    Locked,
    Enabled,
    CanvasW,
    CanvasH,
    ScreenName,
    IconField
};
std::wstring wide(std::string_view s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(size_t(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring contents(HWND control) {
    std::wstring value(size_t(GetWindowTextLengthW(control)) + 1, 0);
    GetWindowTextW(control, value.data(), int(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Windows image export failed");
}
void png(const fs::path &path, UINT w, UINT h, const std::vector<uint8_t> &pixels) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream));
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(w, h));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("Unsupported PNG pixel format");
    check(frame->WritePixels(h, w * 4, UINT(pixels.size()), const_cast<BYTE *>(pixels.data())));
    check(frame->Commit());
    check(encoder->Commit());
}
float numeric(HWND control) {
    std::wistringstream input(contents(control));
    input.imbue(std::locale::classic());
    float value;
    input >> value;
    if (!input || !std::isfinite(value))
        throw std::runtime_error("Enter a finite number");
    input >> std::ws;
    if (!input.eof())
        throw std::runtime_error("Invalid number");
    return value;
}
struct Editor;
LRESULT CALLBACK editorProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK canvasProc(HWND, UINT, WPARAM, LPARAM);
struct Editor {
    LayoutEditor state;
    std::string language{"en"};
    fs::path file, assets;
    HWND window{}, canvas{};
    HFONT font{}, heading{};
    HBRUSH background = CreateSolidBrush(RGB(239, 243, 246));
    struct Child {
        HWND handle;
        int pane, x, y, w, h;
        std::string key;
    };
    std::vector<Child> children;
    bool updating{}, pending{}, pendingCanvas{}, dirty{}, schematic{}, snap{true}, dragging{}, resizing{},
        panning{}, gestureChanged{}, renderDirty{true};
    float zoom{1};
    Vec2 pan{}, last{};
    Rect startBounds{};
    VulkanContext context;
    VulkanRenderer renderer{context};
    Target target;
    std::vector<uint8_t> bitmap;
    std::wstring notice;
    ~Editor() {
        if (target.image)
            renderer.destroyTarget(target);
        if (font)
            DeleteObject(font);
        if (heading)
            DeleteObject(heading);
        DeleteObject(background);
    }
    std::wstring tr(std::string_view key) const { return wide(translate(language, key)); }
    HWND control(int id) const { return GetDlgItem(window, id); }
    void put(int id, std::string_view value) {
        auto text = wide(value);
        SetWindowTextW(control(id), text.c_str());
    }
    void putNumber(int id, float n) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << std::setprecision(6) << n;
        put(id, o.str());
    }
    bool checked(int id) const { return SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void setCheck(int id, bool value) {
        SendMessageW(control(id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void gpu() {
        context.createStandalone();
        renderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, [&](const std::string &name) {
            std::ifstream in(assets / name, std::ios::binary);
            if (!in)
                throw std::runtime_error("Missing asset: " + name);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
        });
    }
    void image(const LayoutScreen &s) {
        auto w = uint32_t(s.width), h = uint32_t(s.height);
        if (target.width != w || target.height != h) {
            if (target.image)
                renderer.destroyTarget(target);
            target = renderer.createTarget(w, h);
        }
        DrawList draw;
        drawLayout(s, draw, nullptr, schematic, state.document.language);
        renderer.draw(target, draw, nullptr, false, {s.width, s.height});
        bitmap = renderer.readback(target);
        for (size_t i = 0; i < bitmap.size(); i += 4) {
            std::swap(bitmap[i], bitmap[i + 2]);
            bitmap[i + 3] = 255;
        }
        renderDirty = false;
    }
    HWND add(const wchar_t *cls, int id, int pane, int x, int y, int w, int h, DWORD style,
             std::string key = {}) {
        HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, L"",
                                 WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        if (!c)
            throw std::runtime_error("Cannot create editor control");
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (wcscmp(cls, L"EDIT") == 0)
            SendMessageW(c, EM_SETLIMITTEXT, id == Text ? 16384 : 256, 0);
        children.push_back({c, pane, x, y, w, h, std::move(key)});
        return c;
    }
    void label(std::string key, int pane, int x, int y, int w = 250) {
        add(L"STATIC", 0, pane, x, y, w, 19, 0, std::move(key));
    }
    void button(int id, std::string key, int pane, int x, int y, int w, int h = 30) {
        add(L"BUTTON", id, pane, x, y, w, h, WS_TABSTOP | BS_PUSHBUTTON, std::move(key));
    }
    void field(int id, std::string key, int x, int y, int w = 250, int h = 26) {
        label(std::move(key), 2, x, y, w);
        add(L"EDIT", id, 2, x, y + 20, w, h,
            WS_TABSTOP | ES_AUTOHSCROLL | (id == Text ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL : 0));
    }
    void menus() {
        HMENU menu = CreateMenu(), f = CreatePopupMenu(), e = CreatePopupMenu(), insert = CreatePopupMenu(),
              view = CreatePopupMenu();
        auto item = [&](HMENU m, int id, const char *key, const wchar_t *shortcut = L"") {
            auto value = tr(key) + shortcut;
            AppendMenuW(m, MF_STRING, id, value.c_str());
        };
        item(f, New, "editor.new", L"\tCtrl+N");
        item(f, FromApp, "editor.from_app");
        item(f, Open, "editor.open", L"\tCtrl+O");
        AppendMenuW(f, MF_SEPARATOR, 0, nullptr);
        item(f, Save, "editor.save", L"\tCtrl+S");
        item(f, SaveAs, "editor.save_as", L"\tCtrl+Shift+S");
        item(f, Export, "editor.export", L"\tCtrl+E");
        item(e, Undo, "editor.undo", L"\tCtrl+Z");
        item(e, Redo, "editor.redo", L"\tCtrl+Y");
        AppendMenuW(e, MF_SEPARATOR, 0, nullptr);
        item(e, Duplicate, "editor.duplicate", L"\tCtrl+D");
        item(e, Delete, "editor.delete", L"\tDel");
        item(e, Front, "editor.front");
        item(e, Back, "editor.back");
        item(insert, AddText, "editor.add_text");
        item(insert, AddButton, "editor.add_button");
        item(insert, AddRect, "editor.add_rect");
        item(insert, AddIcon, "editor.add_icon");
        item(insert, AddScreen, "editor.add_screen");
        item(view, Schematic, "editor.schematic");
        item(view, Snap, "editor.snap");
        item(view, Fit, "editor.fit");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(f), tr("editor.file").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(e), tr("editor.edit").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(insert), tr("editor.insert").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), tr("editor.view").c_str());
        auto old = GetMenu(window);
        SetMenu(window, menu);
        if (old)
            DestroyMenu(old);
        DrawMenuBar(window);
    }
    void localize() {
        updating = true;
        for (auto &c : children)
            if (!c.key.empty())
                SetWindowTextW(c.handle, (c.key.size() == 1 ? wide(c.key) : tr(c.key)).c_str());
        SendMessageW(control(Language), CB_RESETCONTENT, 0, 0);
        SendMessageW(control(Language), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(control(Language), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Français"));
        SendMessageW(control(Language), CB_SETCURSEL, language == "fr" ? 1 : 0, 0);
        menus();
        updating = false;
        title();
        if (window)
            InvalidateRect(window, nullptr, FALSE);
    }
    void create() {
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading =
            CreateFontW(-23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        button(FromApp, "editor.from_app", 0, 18, 52, 118);
        button(Open, "editor.open", 0, 144, 52, 90);
        button(Save, "editor.save", 0, 242, 52, 100);
        button(Export, "editor.export", 0, 350, 52, 112);
        button(Undo, "editor.undo", 0, 470, 52, 84);
        button(Redo, "editor.redo", 0, 562, 52, 84);
        add(L"BUTTON", Schematic, 0, 666, 52, 118, 30, WS_TABSTOP | BS_AUTOCHECKBOX, "editor.schematic");
        add(L"BUTTON", Snap, 0, 794, 52, 120, 30, WS_TABSTOP | BS_AUTOCHECKBOX, "editor.snap");
        setCheck(Snap, true);
        button(Fit, "editor.fit", 0, 920, 52, 70);
        add(L"COMBOBOX", Language, 3, 14, 16, 142, 140, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        label("editor.screens", 1, 18, 108);
        add(L"COMBOBOX", Screens, 1, 18, 134, 202, 230, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        add(L"EDIT", ScreenName, 1, 18, 172, 202, 26, WS_TABSTOP | ES_AUTOHSCROLL);
        label("editor.width", 1, 18, 208, 96);
        label("editor.height", 1, 124, 208, 96);
        add(L"EDIT", CanvasW, 1, 18, 230, 96, 26, WS_TABSTOP | ES_AUTOHSCROLL);
        add(L"EDIT", CanvasH, 1, 124, 230, 96, 26, WS_TABSTOP | ES_AUTOHSCROLL);
        button(ApplyCanvas, "editor.canvas", 1, 18, 264, 202);
        label("editor.layers", 1, 18, 310);
        add(L"LISTBOX", Layers, 1, 18, 336, 202, 360,
            WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_BORDER);
        button(AddText, "editor.add_text", 4, 18, 0, 96);
        button(AddButton, "editor.add_button", 4, 124, 0, 96);
        button(AddRect, "editor.add_rect", 4, 18, 38, 96);
        button(AddIcon, "editor.add_icon", 4, 124, 38, 96);
        label("editor.inspector", 2, 16, 108);
        add(L"STATIC", 999, 2, 16, 134, 254, 32, SS_ENDELLIPSIS);
        field(Name, "editor.name", 16, 172);
        field(X, "X", 16, 222, 120);
        field(Y, "Y", 146, 222, 120);
        field(W, "W", 16, 272, 120);
        field(H, "H", 146, 272, 120);
        field(Text, "editor.text", 16, 322, 250, 54);
        field(Font, "editor.font", 16, 408, 120);
        field(Radius, "editor.radius", 146, 408, 120);
        field(ColorField, "editor.color", 16, 458);
        field(Action, "editor.action", 16, 508, 120);
        field(IconField, "editor.icon", 146, 508, 120);
        field(Key, "editor.key", 16, 558);
        add(L"BUTTON", Visible, 2, 16, 614, 120, 24, WS_TABSTOP | BS_AUTOCHECKBOX, "editor.visible");
        add(L"BUTTON", Locked, 2, 146, 614, 120, 24, WS_TABSTOP | BS_AUTOCHECKBOX, "editor.locked");
        add(L"BUTTON", Enabled, 2, 16, 642, 250, 24, WS_TABSTOP | BS_AUTOCHECKBOX, "editor.enabled");
        button(Apply, "editor.apply", 2, 16, 680, 250);
        button(Duplicate, "editor.duplicate", 2, 16, 720, 120);
        button(Delete, "editor.delete", 2, 146, 720, 120);
        button(Front, "editor.front", 2, 16, 760, 120);
        button(Back, "editor.back", 2, 146, 760, 120);
        canvas = CreateWindowExW(0, L"CyVR.EditorCanvas", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 240, 100,
                                 800, 600, window, nullptr, GetModuleHandleW(nullptr), this);
        if (!canvas)
            throw std::runtime_error("Cannot create editor canvas");
        localize();
        refresh();
        setCheck(Schematic, schematic);
    }
    void arrange() {
        RECT r{};
        GetClientRect(window, &r);
        int right = r.right - 286;
        for (auto &c : children) {
            int x = c.x, y = c.y, w = c.w, h = c.h;
            if (c.pane == 2)
                x += right;
            if (c.pane == 3)
                x = r.right - 158;
            if (c.pane == 4)
                y = r.bottom - 120 + c.y;
            if (GetDlgCtrlID(c.handle) == Layers)
                h = std::max(80, int(r.bottom) - 472);
            MoveWindow(c.handle, x, y, w, h, TRUE);
        }
        MoveWindow(canvas, 238, 100, std::max(100, right - 250), std::max(100, int(r.bottom) - 142), TRUE);
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(canvas, nullptr, FALSE);
    }
    void title() {
        auto value = tr("editor.title") + L" — " +
                     wide(file.empty() ? state.document.name : file.filename().u8string());
        if (dirty || pending || pendingCanvas)
            value += L" *";
        SetWindowTextW(window, value.c_str());
    }
    void change() {
        dirty = true;
        renderDirty = true;
        notice.clear();
        title();
        InvalidateRect(canvas, nullptr, FALSE);
    }
    void refresh() {
        updating = true;
        pending = false;
        pendingCanvas = false;
        SendMessageW(control(Screens), CB_RESETCONTENT, 0, 0);
        for (auto &s : state.document.screens)
            SendMessageW(control(Screens), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(s.name).c_str()));
        SendMessageW(control(Screens), CB_SETCURSEL, state.screenIndex, 0);
        put(ScreenName, state.screen().name);
        putNumber(CanvasW, state.screen().width);
        putNumber(CanvasH, state.screen().height);
        int scroll = int(SendMessageW(control(Layers), LB_GETTOPINDEX, 0, 0));
        SendMessageW(control(Layers), WM_SETREDRAW, FALSE, 0);
        SendMessageW(control(Layers), LB_RESETCONTENT, 0, 0);
        int selected = -1, index = 0;
        for (auto &e : state.screen().elements) {
            std::string label = (e.parent.empty() ? "" : "  ") + e.type + " / " + e.name;
            if (!e.visible)
                label = "[hidden] " + label;
            if (e.locked)
                label = "[lock] " + label;
            SendMessageW(control(Layers), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(label).c_str()));
            if (e.id == state.selection)
                selected = index;
            index++;
        }
        SendMessageW(control(Layers), LB_SETCURSEL, selected, 0);
        SendMessageW(control(Layers), LB_SETTOPINDEX, scroll, 0);
        SendMessageW(control(Layers), WM_SETREDRAW, TRUE, 0);
        InvalidateRect(control(Layers), nullptr, TRUE);
        auto *e = state.selected();
        SetWindowTextW(control(999), e ? wide(e->id + " / " + e->type).c_str() : tr("editor.empty").c_str());
        for (int id = Name; id <= Enabled; id++)
            EnableWindow(control(id), e != nullptr);
        for (int id : {Apply, Duplicate, Delete, Front, Back})
            EnableWindow(control(id), e != nullptr);
        EnableWindow(control(IconField), e && e->type == "icon");
        for (int id : {Name, X, Y, W, H, Text, Font, Radius, ColorField, Action, Key, IconField})
            put(id, "");
        if (e) {
            put(Name, e->name);
            putNumber(X, e->bounds.x);
            putNumber(Y, e->bounds.y);
            putNumber(W, e->bounds.w);
            putNumber(H, e->bounds.h);
            LayoutElement *label = e, *fill = e;
            if (e->type == "group") {
                for (auto &child : state.screen().elements)
                    if (child.parent == e->id) {
                        if (child.type == "text" && label == e)
                            label = &child;
                        if (child.type == "rect" && fill == e)
                            fill = &child;
                    }
            }
            put(Text, label->text);
            put(Key, label->localizationKey);
            putNumber(Font, label->fontSize);
            putNumber(Radius, fill->radius);
            putNumber(Action, float(e->action));
            putNumber(IconField, float(e->icon));
            std::ostringstream color;
            color << std::hex << std::uppercase << std::setfill('0');
            for (float c : {fill->color.r, fill->color.g, fill->color.b, fill->color.a})
                color << std::setw(2) << int(std::round(c * 255));
            put(ColorField, color.str());
            setCheck(Visible, e->visible);
            setCheck(Locked, e->locked);
            setCheck(Enabled, e->enabled);
        }
        updating = false;
        title();
        InvalidateRect(canvas, nullptr, FALSE);
    }
    void apply() {
        if (!pending)
            return;
        auto *e = state.selected();
        if (!e) {
            pending = false;
            return;
        }
        auto backup = state.document;
        LayoutElement *label = e, *fill = e;
        if (e->type == "group")
            for (auto &child : state.screen().elements)
                if (child.parent == e->id) {
                    if (child.type == "text" && label == e)
                        label = &child;
                    if (child.type == "rect" && fill == e)
                        fill = &child;
                }
        auto next = *e, nextLabel = *label, nextFill = *fill;
        next.name = utf8(contents(control(Name)));
        next.bounds = {numeric(control(X)), numeric(control(Y)), numeric(control(W)), numeric(control(H))};
        float action = numeric(control(Action));
        if (action < 0 || action > 1000000 || std::floor(action) != action)
            throw std::runtime_error("Invalid action ID");
        next.action = int(action);
        next.visible = checked(Visible);
        next.locked = checked(Locked);
        next.enabled = checked(Enabled);
        if (e->type == "icon") {
            float icon = numeric(control(IconField));
            if (icon < 0 || icon > 33 || std::floor(icon) != icon)
                throw std::runtime_error("Invalid icon ID");
            next.icon = int(icon);
        }
        nextLabel.text = utf8(contents(control(Text)));
        nextLabel.localizationKey = utf8(contents(control(Key)));
        nextLabel.fontSize = numeric(control(Font));
        nextFill.radius = numeric(control(Radius));
        auto color = utf8(contents(control(ColorField)));
        if (color.size() == 9 && color[0] == '#')
            color.erase(0, 1);
        if (color.size() != 8 || color.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error("Use 8 hexadecimal RGBA digits");
        unsigned long rgba = std::stoul(color, nullptr, 16);
        nextFill.color = {float((rgba >> 24) & 255) / 255, float((rgba >> 16) & 255) / 255,
                          float((rgba >> 8) & 255) / 255, float(rgba & 255) / 255};
        state.checkpoint();
        *e = next;
        label->text = nextLabel.text;
        label->localizationKey = nextLabel.localizationKey;
        label->fontSize = nextLabel.fontSize;
        fill->radius = nextFill.radius;
        fill->color = nextFill.color;
        try {
            validateLayout(state.document);
        } catch (...) {
            state.document = std::move(backup);
            state.undo();
            throw;
        }
        pending = false;
        change();
        refresh();
    }
    void applyCanvas() {
        if (!pendingCanvas)
            return;
        auto name = utf8(contents(control(ScreenName)));
        float w = numeric(control(CanvasW)), h = numeric(control(CanvasH));
        if (w < 64 || w > 4096 || h < 64 || h > 4096 || std::floor(w) != w || std::floor(h) != h)
            throw std::runtime_error("Canvas dimensions must be integers between 64 and 4096");
        if (name.empty() || name.size() > 256)
            throw std::runtime_error("Invalid screen name");
        state.checkpoint();
        state.screen().name = name;
        state.screen().width = w;
        state.screen().height = h;
        pendingCanvas = false;
        change();
        refresh();
    }
    fs::path dialog(bool save, const wchar_t *defaultName = L"CyVR-UI.cyvr.json") {
        wchar_t path[32768]{};
        wcsncpy_s(path, defaultName, _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = window;
        ofn.lpstrFile = path;
        ofn.nMaxFile = 32768;
        ofn.lpstrFilter = L"CyVR layout (*.json)\0*.json\0All files\0*.*\0";
        ofn.lpstrDefExt = L"json";
        ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))
            return fs::path(path);
        return {};
    }
    bool save(bool as = false) {
        apply();
        applyCanvas();
        auto path = file;
        if (as || path.empty())
            path = dialog(true, file.empty() ? L"CyVR-UI.cyvr.json" : file.c_str());
        if (path.empty())
            return false;
        saveLayout(path.u8string(), state.document);
        file = path;
        dirty = false;
        notice = tr("editor.saved");
        title();
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    bool discard() {
        if (!dirty && !pending && !pendingCanvas)
            return true;
        int answer = MessageBoxW(window, tr("editor.unsaved").c_str(), tr("editor.title").c_str(),
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return false;
        return answer == IDNO || save();
    }
    void select(std::string id) {
        apply();
        applyCanvas();
        state.selection = std::move(id);
        refresh();
    }
    void exportAI(const fs::path &path) {
        saveLayout(path.u8string(), state.document);
        auto markdown = path;
        markdown.replace_extension(L".md");
        std::ofstream out(markdown, std::ios::binary);
        out << layoutOutline(state.document);
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write outline");
        for (auto &s : state.document.screens) {
            image(s);
            auto preview = path.parent_path() / (path.stem().wstring() + L"." + wide(s.id) + L".png");
            png(preview, UINT(s.width), UINT(s.height), bitmap);
        }
        renderDirty = true;
        notice = tr("editor.exported");
        if (window)
            InvalidateRect(window, nullptr, FALSE);
    }
    void command(int id) {
        switch (id) {
        case New:
        case FromApp:
            if (!discard())
                return;
            if (id == FromApp)
                state.reset(referenceLayout(language));
            else {
                LayoutDocument doc;
                doc.language = language;
                doc.screens.push_back({});
                state.reset(std::move(doc));
            }
            file.clear();
            dirty = false;
            zoom = 1;
            pan = {};
            renderDirty = true;
            refresh();
            break;
        case Open: {
            if (!discard())
                return;
            auto path = dialog(false);
            if (path.empty())
                return;
            auto doc = loadLayout(path.u8string());
            state.reset(std::move(doc));
            file = path;
            language = state.document.language == "fr" ? "fr" : "en";
            dirty = false;
            zoom = 1;
            pan = {};
            renderDirty = true;
            localize();
            refresh();
            break;
        }
        case Save:
            save();
            break;
        case SaveAs:
            save(true);
            break;
        case Export: {
            apply();
            applyCanvas();
            auto path = dialog(true, L"CyVR-UI.ai.json");
            if (!path.empty())
                exportAI(path);
            break;
        }
        case Schematic:
            schematic = !schematic;
            setCheck(Schematic, schematic);
            renderDirty = true;
            InvalidateRect(canvas, nullptr, FALSE);
            break;
        case Snap:
            snap = !snap;
            setCheck(Snap, snap);
            break;
        case Fit:
            zoom = 1;
            pan = {};
            InvalidateRect(canvas, nullptr, FALSE);
            break;
        case Apply:
            apply();
            break;
        case ApplyCanvas:
            applyCanvas();
            break;
        case Undo:
            pending = false;
            pendingCanvas = false;
            if (state.undo()) {
                change();
                refresh();
            }
            break;
        case Redo:
            pending = false;
            pendingCanvas = false;
            if (state.redo()) {
                change();
                refresh();
            }
            break;
        case AddText:
        case AddButton:
        case AddRect:
        case AddIcon:
            apply();
            applyCanvas();
            state.checkpoint();
            state.add(id == AddText ? "text" : id == AddButton ? "button" : id == AddIcon ? "icon" : "rect");
            change();
            refresh();
            break;
        case Duplicate:
        case Delete:
        case Front:
        case Back:
            apply();
            applyCanvas();
            if (!state.selected() || state.selected()->locked)
                return;
            state.checkpoint();
            if (id == Duplicate)
                state.duplicateSelected();
            else if (id == Delete)
                state.removeSelected();
            else
                state.reorderSelected(id == Front ? 1 : -1);
            change();
            refresh();
            break;
        case AddScreen: {
            apply();
            applyCanvas();
            if (state.document.screens.size() >= 64)
                return;
            state.checkpoint();
            LayoutScreen s;
            s.id = "screen-" + std::to_string(state.document.screens.size() + 1);
            for (int n = 1; std::any_of(state.document.screens.begin(), state.document.screens.end(),
                                        [&](auto &old) { return old.id == s.id; });
                 n++)
                s.id = "new-screen-" + std::to_string(n);
            s.name = s.id;
            state.document.screens.push_back(s);
            state.screenIndex = state.document.screens.size() - 1;
            state.selection.clear();
            change();
            refresh();
            break;
        }
        }
    }
    CanvasMapping mapping() const {
        RECT r{};
        GetClientRect(canvas, &r);
        auto map = fitCanvas(std::max(1, int(r.right) - 64), std::max(1, int(r.bottom) - 86),
                             state.screen().width, state.screen().height);
        map.scale *= zoom;
        map.viewport.w = state.screen().width * map.scale;
        map.viewport.h = state.screen().height * map.scale;
        map.viewport.x = (r.right - map.viewport.w) / 2 + pan.x;
        map.viewport.y = (r.bottom - map.viewport.h) / 2 + pan.y + 12;
        return map;
    }
    void paintCanvas(HDC dc) {
        RECT client{};
        GetClientRect(canvas, &client);
        FillRect(dc, &client, background);
        auto map = mapping();
        auto r = map.viewport;
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, font);
        SetTextColor(dc, RGB(76, 94, 104));
        auto title = wide(state.screen().name) + L"  /  " + std::to_wstring(int(state.screen().width)) +
                     L" × " + std::to_wstring(int(state.screen().height));
        TextOutW(dc, 22, 12, title.c_str(), int(title.size()));
        if (renderDirty)
            image(state.screen());
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = LONG(target.width);
        info.bmiHeader.biHeight = -LONG(target.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, int(r.x), int(r.y), int(r.w), int(r.h), 0, 0, int(target.width), int(target.height),
                      bitmap.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        if (auto *e = state.selected()) {
            auto bounds = layoutBounds(state.screen(), *e);
            auto p = map.toSurface({bounds.x, bounds.y});
            HPEN pen = CreatePen(PS_SOLID, 2, e->locked ? RGB(180, 140, 64) : RGB(22, 161, 136));
            auto old = SelectObject(dc, pen);
            auto brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, int(p.x), int(p.y), int(p.x + bounds.w * map.scale),
                      int(p.y + bounds.h * map.scale));
            if (!e->locked) {
                RECT handle{LONG(p.x + bounds.w * map.scale - 5), LONG(p.y + bounds.h * map.scale - 5),
                            LONG(p.x + bounds.w * map.scale + 5), LONG(p.y + bounds.h * map.scale + 5)};
                FillRect(dc, &handle, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                Rectangle(dc, handle.left, handle.top, handle.right, handle.bottom);
            }
            SelectObject(dc, brush);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
    void paintChrome(HDC dc) {
        RECT r{};
        GetClientRect(window, &r);
        FillRect(dc, &r, background);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, heading);
        SetTextColor(dc, RGB(24, 42, 50));
        auto name = tr("editor.title");
        TextOutW(dc, 18, 12, name.c_str(), int(name.size()));
        SelectObject(dc, font);
        SetTextColor(dc, RGB(80, 98, 108));
        auto hint = notice.empty() ? tr("editor.hint") : notice;
        RECT text{18, r.bottom - 30, r.right - 12, r.bottom - 4};
        DrawTextW(dc, hint.c_str(), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    void snapshotWindow(const fs::path &path) {
        RECT r{};
        GetClientRect(window, &r);
        UINT w = UINT(r.right), h = UINT(r.bottom);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = LONG(w);
        info.bmiHeader.biHeight = -LONG(h);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP image = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !image)
            throw std::runtime_error("Cannot create editor preview");
        auto previous = SelectObject(dc, image);
        paintChrome(dc);
        auto printChild = [&](HWND child) {
            RECT bounds{};
            GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&bounds), 2);
            int saved = SaveDC(dc);
            SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
            IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                         PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
            RestoreDC(dc, saved);
        };
        for (auto &child : children)
            printChild(child.handle);
        printChild(canvas);
        GdiFlush();
        std::vector<uint8_t> bytes(static_cast<uint8_t *>(pixels),
                                   static_cast<uint8_t *>(pixels) + size_t(w) * h * 4);
        for (size_t i = 3; i < bytes.size(); i += 4)
            bytes[i] = 255;
        SelectObject(dc, previous);
        DeleteObject(image);
        DeleteDC(dc);
        png(path, w, h, bytes);
    }
    void pointerDown(Vec2 point, bool middle) {
        SetFocus(canvas);
        auto map = mapping();
        if (middle) {
            panning = true;
            last = point;
            SetCapture(canvas);
            return;
        }
        apply();
        applyCanvas();
        if (auto *e = state.selected(); e && !e->locked) {
            auto r = layoutBounds(state.screen(), *e);
            auto handle = map.toSurface({r.x + r.w, r.y + r.h});
            resizing = std::abs(handle.x - point.x) < 10 && std::abs(handle.y - point.y) < 10;
        }
        if (!resizing)
            select(state.pick(map.toCanvas(point), (GetKeyState(VK_MENU) & 0x8000) != 0));
        gestureChanged = false;
        if (auto *e = state.selected(); e && !e->locked) {
            startBounds = e->bounds;
            last = point;
            dragging = true;
            SetCapture(canvas);
        }
    }
    void pointerMove(Vec2 point) {
        if (panning) {
            pan.x += point.x - last.x;
            pan.y += point.y - last.y;
            last = point;
            InvalidateRect(canvas, nullptr, FALSE);
            return;
        }
        if (!dragging)
            return;
        auto *e = state.selected();
        if (!e)
            return;
        auto map = mapping();
        float dx = (point.x - last.x) / map.scale, dy = (point.y - last.y) / map.scale;
        last = point;
        if (!dx && !dy)
            return;
        if (!gestureChanged) {
            state.checkpoint();
            gestureChanged = true;
        }
        if (resizing) {
            auto r = layoutBounds(state.screen(), *e);
            state.resizeSelected(e->bounds.w + dx * e->bounds.w / r.w, e->bounds.h + dy * e->bounds.h / r.h,
                                 false);
        } else
            state.moveSelected(dx, dy, false);
        change();
    }
    void pointerUp() {
        if (dragging && gestureChanged && snap && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            if (auto *e = state.selected()) {
                if (resizing)
                    state.resizeSelected(e->bounds.w, e->bounds.h, true);
                else
                    state.moveSelected(0, 0, true);
                change();
            }
        }
        dragging = resizing = panning = false;
        ReleaseCapture();
        refresh();
    }
};
LRESULT CALLBACK canvasProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto *app = reinterpret_cast<Editor *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<Editor *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, w, l);
    try {
        Vec2 point{float(GET_X_LPARAM(l)), float(GET_Y_LPARAM(l))};
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PRINTCLIENT:
            app->paintCanvas(reinterpret_cast<HDC>(w));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            auto dc = BeginPaint(hwnd, &paint);
            try {
                app->paintCanvas(dc);
            } catch (...) {
                EndPaint(hwnd, &paint);
                throw;
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            app->pointerDown(point, false);
            return 0;
        case WM_MBUTTONDOWN:
            app->pointerDown(point, true);
            return 0;
        case WM_MOUSEMOVE:
            app->pointerMove(point);
            return 0;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
            app->pointerUp();
            return 0;
        case WM_CAPTURECHANGED:
            app->dragging = app->resizing = app->panning = false;
            return 0;
        case WM_MOUSEWHEEL:
            app->zoom =
                std::clamp(app->zoom * (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1.15f : 1 / 1.15f), .25f, 4.f);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    } catch (const std::exception &e) {
        MessageBoxW(hwnd, wide(e.what()).c_str(), app->tr("editor.error").c_str(), MB_ICONERROR);
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
LRESULT CALLBACK editorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto *app = reinterpret_cast<Editor *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<Editor *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, w, l);
    try {
        switch (msg) {
        case WM_CREATE:
            app->create();
            return 0;
        case WM_SIZE:
            app->arrange();
            return 0;
        case WM_GETMINMAXINFO: {
            auto *info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {1200, 910};
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PRINTCLIENT:
            app->paintChrome(reinterpret_cast<HDC>(w));
            return 0;
        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(w);
            SetBkColor(dc, RGB(239, 243, 246));
            SetTextColor(dc, RGB(49, 66, 76));
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            app->paintChrome(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(w), notification = HIWORD(w);
            if (app->updating)
                return 0;
            if (id == Screens && notification == CBN_SELCHANGE) {
                auto index = SendMessageW(app->control(Screens), CB_GETCURSEL, 0, 0);
                app->apply();
                app->applyCanvas();
                if (index >= 0) {
                    app->state.screenIndex = size_t(index);
                    app->state.selection.clear();
                    app->zoom = 1;
                    app->pan = {};
                    app->renderDirty = true;
                    app->refresh();
                }
                return 0;
            }
            if (id == Layers && notification == LBN_SELCHANGE) {
                auto index = SendMessageW(app->control(Layers), LB_GETCURSEL, 0, 0);
                if (index >= 0)
                    app->select(app->state.screen().elements.at(size_t(index)).id);
                return 0;
            }
            if (id == Language && notification == CBN_SELCHANGE) {
                app->language = SendMessageW(app->control(Language), CB_GETCURSEL, 0, 0) == 1 ? "fr" : "en";
                app->localize();
                return 0;
            }
            if (notification == EN_CHANGE && ((id >= Name && id <= Key) || id == CanvasW || id == CanvasH ||
                                              id == ScreenName || id == IconField)) {
                if (id == CanvasW || id == CanvasH || id == ScreenName)
                    app->pendingCanvas = true;
                else
                    app->pending = true;
                app->title();
                return 0;
            }
            if (id == Visible || id == Locked || id == Enabled) {
                app->pending = true;
                app->title();
                return 0;
            }
            if (notification == BN_CLICKED || l == 0)
                app->command(id);
            return 0;
        }
        case WM_CLOSE:
            if (app->discard())
                DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &e) {
        MessageBoxW(hwnd, wide(e.what()).c_str(), app->tr("editor.error").c_str(), MB_ICONERROR);
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
bool shortcut(Editor &app, MSG &msg) {
    if (msg.message != WM_KEYDOWN)
        return false;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    wchar_t cls[64]{};
    GetClassNameW(GetFocus(), cls, 64);
    bool edit = wcscmp(cls, L"EDIT") == 0;
    int command = 0;
    if (ctrl) {
        switch (msg.wParam) {
        case 'S':
            command = shift ? SaveAs : Save;
            break;
        case 'O':
            command = Open;
            break;
        case 'N':
            command = New;
            break;
        case 'E':
            command = Export;
            break;
        case 'D':
            command = edit ? 0 : Duplicate;
            break;
        case 'Z':
            command = edit ? 0 : Undo;
            break;
        case 'Y':
            command = edit ? 0 : Redo;
            break;
        case 'A':
            if (edit) {
                SendMessageW(GetFocus(), EM_SETSEL, 0, -1);
                return true;
            }
            break;
        }
    } else if (!edit && msg.wParam == VK_DELETE)
        command = Delete;
    else if (edit && msg.wParam == VK_RETURN && GetDlgCtrlID(GetFocus()) != Text) {
        int id = GetDlgCtrlID(GetFocus());
        command = (id == CanvasW || id == CanvasH || id == ScreenName) ? ApplyCanvas : Apply;
    }
    if (command) {
        app.command(command);
        return true;
    }
    if (!edit &&
        (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT || msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
        app.state.checkpoint();
        float step = shift ? 8.f : 1.f;
        app.state.moveSelected(msg.wParam == VK_LEFT    ? -step
                               : msg.wParam == VK_RIGHT ? step
                                                        : 0,
                               msg.wParam == VK_UP     ? -step
                               : msg.wParam == VK_DOWN ? step
                                                       : 0,
                               false);
        app.change();
        app.refresh();
        return true;
    }
    return false;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        SetProcessDPIAware();
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        fs::path dir = fs::path(executable).parent_path();
        Editor app;
        app.assets = dir / "assets";
        fs::path open, snapshot, windowSnapshot, exportPath;
        std::string exportReference;
        bool selfTest = false, validateOnly = false;
        for (int i = 1; i < argc; i++) {
            std::string arg = utf8(argv[i]);
            auto next = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing command argument");
                return utf8(argv[++i]);
            };
            if (arg == "--assets")
                app.assets = fs::u8path(next());
            else if (arg == "--open")
                open = fs::u8path(next());
            else if (arg == "--language")
                app.language = next();
            else if (arg == "--schematic")
                app.schematic = true;
            else if (arg == "--export-reference")
                exportReference = next();
            else if (arg == "--snapshot")
                snapshot = fs::u8path(next());
            else if (arg == "--snapshot-editor")
                windowSnapshot = fs::u8path(next());
            else if (arg == "--export-ai")
                exportPath = fs::u8path(next());
            else if (arg == "--self-test")
                selfTest = true;
            else if (arg == "--validate")
                validateOnly = true;
            else if (arg == "--screen") {
                app.state.screenIndex = std::stoul(next());
            } else if (arg == "--help") {
                std::cout
                    << "CyVR_Editor [--open file.json] [--language en|fr] [--schematic] [--screen index]\n"
                    << "  --validate  Check the document without a GPU or window\n"
                    << "  --export-reference file.json  Capture the native 2D catalogue\n  --export-ai "
                       "file.json  Export JSON, Markdown and screen PNGs\n  --snapshot file.png  Render "
                       "current screen without opening a window\n";
                return 0;
            } else if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".json")
                open = fs::u8path(arg);
            else
                throw std::runtime_error("Unknown argument: " + arg);
        }
        auto screenIndex = app.state.screenIndex;
        app.state.reset(open.empty() ? referenceLayout(app.language) : loadLayout(open.u8string()));
        if (screenIndex >= app.state.document.screens.size())
            throw std::runtime_error("Screen index out of range");
        app.state.screenIndex = screenIndex;
        app.file = open;
        if (validateOnly) {
            validateLayout(app.state.document);
            std::cout << "Layout document verified\n";
            return 0;
        }
        if (!exportReference.empty()) {
            saveLayout(exportReference, app.state.document);
            std::cout << "Reference layout exported\n";
            return 0;
        }
        app.gpu();
        if (!snapshot.empty()) {
            app.image(app.state.screen());
            png(snapshot, UINT(app.state.screen().width), UINT(app.state.screen().height), app.bitmap);
            std::cout << "Layout snapshot written\n";
            return 0;
        }
        if (!exportPath.empty()) {
            app.exportAI(exportPath);
            std::cout << "AI bundle exported\n";
            return 0;
        }
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cls.lpszClassName = L"CyVR.EditorCanvas";
        cls.lpfnWndProc = canvasProc;
        RegisterClassW(&cls);
        cls.lpszClassName = L"CyVR.InterfaceEditor";
        cls.lpfnWndProc = editorProc;
        RegisterClassW(&cls);
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"CyVR Interface Editor",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                      1536, 1000, nullptr, nullptr, cls.hInstance, &app);
        if (!window)
            throw std::runtime_error("Cannot create editor window");
        if (!windowSnapshot.empty()) {
            app.select("control-100");
            app.snapshotWindow(windowSnapshot);
            DestroyWindow(window);
            std::cout << "Editor preview written\n";
            return 0;
        }
        if (selfTest) {
            if (SendMessageW(app.control(Screens), CB_GETCOUNT, 0, 0) != 6 ||
                SendMessageW(app.control(Screens), CB_GETCURSEL, 0, 0) != 0 ||
                SendMessageW(app.control(Language), CB_GETCOUNT, 0, 0) != 2)
                throw std::runtime_error("Native screen/language selection failed");
            app.select("control-100");
            if (!app.state.selected())
                throw std::runtime_error("Inspector selection failed");
            app.putNumber(X, 320);
            app.pending = true;
            app.apply();
            if (app.state.selected()->bounds.x != 320)
                throw std::runtime_error("Property edit failed");
            app.command(Undo);
            app.command(Redo);
            if (app.state.selected()->bounds.x != 320)
                throw std::runtime_error("Native undo/redo failed");
            auto bounds = app.state.selected()->bounds;
            auto map = app.mapping();
            auto point = map.toSurface({bounds.x + 20, bounds.y + 20});
            app.pointerDown(point, false);
            app.pointerUp();
            if (app.state.selected()->bounds.x != bounds.x || app.state.selected()->bounds.y != bounds.y)
                throw std::runtime_error("A click moved the element");
            app.pointerDown(point, false);
            app.pointerMove({point.x + 24 * map.scale, point.y + 16 * map.scale});
            app.pointerUp();
            if (app.state.selected()->bounds.x != 344)
                throw std::runtime_error("Canvas drag failed");
            app.command(Duplicate);
            app.command(Delete);
            app.command(AddText);
            app.command(Schematic);
            app.image(app.state.screen());
            if (app.bitmap.empty())
                throw std::runtime_error("Canvas render failed");
            DestroyWindow(window);
            std::cout << "Native editor controls, inspector, undo/redo and rendering passed\n";
            return 0;
        }
        FreeConsole();
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            try {
                if (shortcut(app, msg))
                    continue;
            } catch (const std::exception &e) {
                MessageBoxW(window, wide(e.what()).c_str(), app.tr("editor.error").c_str(), MB_ICONERROR);
                continue;
            }
            if (!IsDialogMessageW(window, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return int(msg.wParam);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
