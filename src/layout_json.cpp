// SPDX-License-Identifier: MIT
#include "cyvr/icons.hpp"
#include "cyvr/layout.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cyvr {
namespace {
[[noreturn]] void fail(const std::string &s) {
    throw std::runtime_error("Layout: " + s);
}
bool utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i++];
        if (c < 128)
            continue;
        int n;
        uint32_t code;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            code = c & 31;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            code = c & 15;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            code = c & 7;
        } else
            return false;
        if (i + size_t(n) > s.size())
            return false;
        for (int j = 0; j < n; j++) {
            unsigned char b = s[i++];
            if ((b & 192) != 128)
                return false;
            code = (code << 6) | (b & 63);
        }
        if (code < (n == 1   ? 0x80u
                    : n == 2 ? 0x800u
                             : 0x10000u) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
struct Json {
    enum Kind { Null, Number, String, Boolean, Array, Object } kind{Null};
    double number{};
    bool boolean{};
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};
class Parser {
    std::string_view text;
    size_t i{}, values{};
    [[noreturn]] void error() const { fail("invalid JSON near byte " + std::to_string(i)); }
    void ws() {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\t' || text[i] == '\r'))
            i++;
    }
    unsigned hex4() {
        unsigned n = 0;
        for (int k = 0; k < 4; k++) {
            if (i >= text.size())
                error();
            char c = text[i++];
            n <<= 4;
            if (c >= '0' && c <= '9')
                n += c - '0';
            else if (c >= 'a' && c <= 'f')
                n += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                n += c - 'A' + 10;
            else
                error();
        }
        return n;
    }
    std::string str() {
        if (i >= text.size() || text[i++] != '"')
            error();
        std::string out;
        while (i < text.size()) {
            unsigned char c = text[i++];
            if (c == '"') {
                if (!utf8(out))
                    error();
                return out;
            }
            if (c < 32)
                error();
            if (c != '\\')
                out += char(c);
            else {
                if (i >= text.size())
                    error();
                char e = text[i++];
                switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    unsigned code = hex4();
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (i + 2 > text.size() || text.substr(i, 2) != "\\u")
                            error();
                        i += 2;
                        unsigned lo = hex4();
                        if (lo < 0xdc00 || lo > 0xdfff)
                            error();
                        code = 0x10000 + ((code - 0xd800) << 10) + lo - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff)
                        error();
                    if (code < 128)
                        out += char(code);
                    else if (code < 2048) {
                        out += char(192 | (code >> 6));
                        out += char(128 | (code & 63));
                    } else if (code < 65536) {
                        out += char(224 | (code >> 12));
                        out += char(128 | ((code >> 6) & 63));
                        out += char(128 | (code & 63));
                    } else {
                        out += char(240 | (code >> 18));
                        out += char(128 | ((code >> 12) & 63));
                        out += char(128 | ((code >> 6) & 63));
                        out += char(128 | (code & 63));
                    }
                    break;
                }
                default:
                    error();
                }
            }
            if (out.size() > 16384)
                error();
        }
        error();
    }
    Json value(int depth) {
        if (depth > 32 || ++values > 300000)
            error();
        ws();
        if (i >= text.size())
            error();
        Json v;
        char c = text[i];
        if (c == '"') {
            v.kind = Json::String;
            v.string = str();
            return v;
        }
        if (c == '{' || c == '[') {
            bool object = c == '{';
            v.kind = object ? Json::Object : Json::Array;
            i++;
            ws();
            if (i < text.size() && text[i] == (object ? '}' : ']')) {
                i++;
                return v;
            }
            for (;;) {
                ws();
                std::string key;
                if (object) {
                    key = str();
                    ws();
                    if (i >= text.size() || text[i++] != ':')
                        error();
                }
                auto child = value(depth + 1);
                if (object) {
                    if (!v.object.emplace(key, std::move(child)).second)
                        error();
                } else
                    v.array.push_back(std::move(child));
                ws();
                if (i >= text.size())
                    error();
                char end = text[i++];
                if (end == (object ? '}' : ']'))
                    break;
                if (end != ',')
                    error();
            }
            return v;
        }
        if (text.substr(i, 4) == "true") {
            i += 4;
            v.kind = Json::Boolean;
            v.boolean = true;
            return v;
        }
        if (text.substr(i, 5) == "false") {
            i += 5;
            v.kind = Json::Boolean;
            return v;
        }
        if (text.substr(i, 4) == "null") {
            i += 4;
            return v;
        }
        size_t begin = i;
        if (c == '-')
            i++;
        if (i >= text.size())
            error();
        if (text[i] == '0')
            i++;
        else {
            if (text[i] < '1' || text[i] > '9')
                error();
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                i++;
        }
        if (i < text.size() && text[i] == '.') {
            i++;
            size_t b = i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                i++;
            if (b == i)
                error();
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            i++;
            if (i < text.size() && (text[i] == '+' || text[i] == '-'))
                i++;
            size_t b = i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                i++;
            if (b == i)
                error();
        }
        std::istringstream in(std::string(text.substr(begin, i - begin)));
        in.imbue(std::locale::classic());
        v.kind = Json::Number;
        in >> v.number;
        if (!in || !std::isfinite(v.number))
            error();
        return v;
    }

  public:
    explicit Parser(std::string_view s) : text(s) {}
    Json parse() {
        if (text.size() > 8 * 1024 * 1024)
            fail("file exceeds 8 MiB");
        auto v = value(0);
        ws();
        if (i != text.size())
            error();
        return v;
    }
};
const Json &member(const Json &j, const char *key) {
    if (j.kind != Json::Object)
        fail("expected object");
    auto it = j.object.find(key);
    if (it == j.object.end())
        fail(std::string("missing ") + key);
    return it->second;
}
std::string string(const Json &v) {
    if (v.kind != Json::String)
        fail("expected string");
    return v.string;
}
double number(const Json &v) {
    if (v.kind != Json::Number)
        fail("expected number");
    return v.number;
}
bool boolean(const Json &v) {
    if (v.kind != Json::Boolean)
        fail("expected boolean");
    return v.boolean;
}
const std::vector<Json> &array(const Json &v) {
    if (v.kind != Json::Array)
        fail("expected array");
    return v.array;
}
int integer(const Json &v) {
    auto n = number(v);
    if (n < 0 || n > 1000000 || std::floor(n) != n)
        fail("expected nonnegative integer");
    return int(n);
}
std::vector<float> tuple(const Json &v, size_t count) {
    auto &a = array(v);
    if (a.size() != count)
        fail("invalid coordinate count");
    std::vector<float> result;
    for (auto &x : a) {
        auto n = number(x);
        if (std::abs(n) > 1e6)
            fail("coordinate too large");
        result.push_back(float(n));
    }
    return result;
}
Rect rect(const Json &j) {
    auto a = tuple(j, 4);
    return {a[0], a[1], a[2], a[3]};
}
std::string quote(std::string_view s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if (c < 32)
                o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else
                o << char(c);
        }
    }
    o << '"';
    return o.str();
}
void checkString(const std::string &s, size_t max) {
    if (s.size() > max || !utf8(s) || s.find('\0') != std::string::npos)
        fail("invalid or oversized UTF-8 string");
}
bool identifier(const std::string &s) {
    return !s.empty() && s.size() <= 96 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '-' || c == '.';
    });
}
bool bounded(float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}
} // namespace
void validateLayout(const LayoutDocument &doc) {
    checkString(doc.name, 256);
    checkString(doc.language, 32);
    if (doc.screens.empty() || doc.screens.size() > 64)
        fail("expected 1 to 64 screens");
    std::set<std::string> screens;
    size_t total = 0;
    const std::set<std::string> types{"group", "rect", "border", "text",     "icon",  "logo",  "circle",
                                      "ring",  "line", "path",   "triangle", "image", "video", "overlay"};
    for (auto &s : doc.screens) {
        if (!identifier(s.id) || !screens.insert(s.id).second)
            fail("invalid or duplicate screen ID");
        checkString(s.name, 256);
        if (!bounded(s.width, 64, 4096) || !bounded(s.height, 64, 4096))
            fail("invalid canvas");
        total += s.elements.size();
        if (total > 8192)
            fail("too many elements");
        std::map<std::string, const LayoutElement *> ids;
        for (auto &e : s.elements) {
            if (!identifier(e.id) || !ids.emplace(e.id, &e).second)
                fail("invalid or duplicate element ID");
            if (!types.count(e.type))
                fail("unknown element type");
            checkString(e.name, 256);
            checkString(e.text, 16384);
            checkString(e.localizationKey, 256);
            if (!bounded(e.bounds.x, -32768, 32768) || !bounded(e.bounds.y, -32768, 32768) ||
                !bounded(e.bounds.w, 1, 8192) || !bounded(e.bounds.h, 1, 8192))
                fail("invalid bounds");
            if (!bounded(e.viewBox.x, 1, 8192) || !bounded(e.viewBox.y, 1, 8192))
                fail("invalid group viewBox");
            if (!bounded(e.color.r, 0, 1) || !bounded(e.color.g, 0, 1) || !bounded(e.color.b, 0, 1) ||
                !bounded(e.color.a, 0, 1))
                fail("invalid color");
            if (!bounded(e.uv.x, -16, 16) || !bounded(e.uv.y, -16, 16) || !bounded(e.uv.w, -16, 16) ||
                !bounded(e.uv.h, -16, 16))
                fail("invalid texture coordinates");
            if (!bounded(e.radius, 0, 4096) || !bounded(e.stroke, .1f, 128) || !bounded(e.fontSize, 1, 512))
                fail("invalid style");
            if (e.icon < 0 || e.icon > int(Icon::NewWindow) || e.action < 0 || e.action > 1000000)
                fail("invalid icon/action");
            if (e.points.size() > 512)
                fail("too many path points");
            for (auto p : e.points)
                if (!bounded(p.x, -1, 2) || !bounded(p.y, -1, 2))
                    fail("invalid path point");
            if ((e.type == "line" && e.points.size() != 2) ||
                (e.type == "triangle" && e.points.size() != 3) || (e.type == "path" && e.points.size() < 2))
                fail("invalid path");
        }
        for (auto &e : s.elements) {
            std::set<std::string> chain{e.id};
            auto *p = &e;
            while (!p->parent.empty()) {
                auto it = ids.find(p->parent);
                if (it == ids.end() || it->second->type != "group" || !chain.insert(p->parent).second ||
                    chain.size() > 32)
                    fail("invalid group hierarchy");
                p = it->second;
            }
            auto r = layoutBounds(s, e);
            if (!bounded(r.x, -65536, 65536) || !bounded(r.y, -65536, 65536) || !bounded(r.w, .01f, 65536) ||
                !bounded(r.h, .01f, 65536))
                fail("group transform exceeds limits");
        }
    }
}
LayoutDocument parseLayout(std::string_view text) {
    auto root = Parser(text).parse();
    if (string(member(root, "format")) != "cyvr-ui-layout" || integer(member(root, "version")) != 1)
        fail("unsupported format/version");
    LayoutDocument doc;
    doc.name = string(member(root, "name"));
    doc.language = string(member(root, "language"));
    for (auto &j : array(member(root, "screens"))) {
        LayoutScreen s;
        s.id = string(member(j, "id"));
        s.name = string(member(j, "name"));
        auto canvas = tuple(member(j, "canvas"), 2);
        s.width = canvas[0];
        s.height = canvas[1];
        for (auto &n : array(member(j, "elements"))) {
            LayoutElement e;
            e.id = string(member(n, "id"));
            e.name = string(member(n, "name"));
            e.type = string(member(n, "type"));
            e.bounds = rect(member(n, "bounds"));
            for (auto &pair : n.object) {
                const auto &k = pair.first;
                const auto &v = pair.second;
                if (k == "parent")
                    e.parent = string(v);
                else if (k == "text")
                    e.text = string(v);
                else if (k == "localizationKey")
                    e.localizationKey = string(v);
                else if (k == "color") {
                    auto a = tuple(v, 4);
                    e.color = {a[0], a[1], a[2], a[3]};
                } else if (k == "viewBox") {
                    auto a = tuple(v, 2);
                    e.viewBox = {a[0], a[1]};
                } else if (k == "uv")
                    e.uv = rect(v);
                else if (k == "radius")
                    e.radius = float(number(v));
                else if (k == "stroke")
                    e.stroke = float(number(v));
                else if (k == "fontSize")
                    e.fontSize = float(number(v));
                else if (k == "icon")
                    e.icon = integer(v);
                else if (k == "action")
                    e.action = integer(v);
                else if (k == "visible")
                    e.visible = boolean(v);
                else if (k == "locked")
                    e.locked = boolean(v);
                else if (k == "enabled")
                    e.enabled = boolean(v);
                else if (k == "closed")
                    e.closed = boolean(v);
                else if (k == "points") {
                    for (auto &p : array(v)) {
                        auto a = tuple(p, 2);
                        e.points.push_back({a[0], a[1]});
                    }
                } else if (k == "control") {
                    auto type = string(v);
                    if (type == "button")
                        e.control = Control::Button;
                    else if (type == "slider")
                        e.control = Control::Slider;
                    else if (type == "drag")
                        e.control = Control::Drag;
                    else
                        fail("unknown control type");
                } else if (k != "id" && k != "name" && k != "type" && k != "bounds")
                    fail("unknown element property: " + k);
            }
            s.elements.push_back(std::move(e));
        }
        doc.screens.push_back(std::move(s));
    }
    validateLayout(doc);
    return doc;
}
std::string serializeLayout(const LayoutDocument &doc) {
    validateLayout(doc);
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(7);
    o << "{\n  \"format\": \"cyvr-ui-layout\",\n  \"version\": 1,\n  \"name\": " << quote(doc.name)
      << ",\n  \"language\": " << quote(doc.language) << ",\n  \"screens\": [\n";
    for (size_t i = 0; i < doc.screens.size(); i++) {
        const auto &s = doc.screens[i];
        o << "    {\"id\": " << quote(s.id) << ", \"name\": " << quote(s.name) << ", \"canvas\": [" << s.width
          << ", " << s.height << "],\n     \"elements\": [\n";
        for (size_t j = 0; j < s.elements.size(); j++) {
            const auto &e = s.elements[j];
            o << "      {\"id\": " << quote(e.id) << ", \"name\": " << quote(e.name)
              << ", \"type\": " << quote(e.type);
            if (!e.parent.empty())
                o << ", \"parent\": " << quote(e.parent);
            o << ", \"bounds\": [" << e.bounds.x << ", " << e.bounds.y << ", " << e.bounds.w << ", "
              << e.bounds.h << "]";
            o << ", \"color\": [" << e.color.r << ", " << e.color.g << ", " << e.color.b << ", " << e.color.a
              << "]";
            if (e.type == "group") {
                o << ", \"viewBox\": [" << e.viewBox.x << ", " << e.viewBox.y << "]";
                o << ", \"action\": " << e.action << ", \"control\": "
                  << quote(e.control == Control::Slider ? "slider"
                           : e.control == Control::Drag ? "drag"
                                                        : "button");
            }
            if (e.type == "text") {
                o << ", \"text\": " << quote(e.text) << ", \"fontSize\": " << e.fontSize;
                if (!e.localizationKey.empty())
                    o << ", \"localizationKey\": " << quote(e.localizationKey);
            }
            if (e.type == "icon")
                o << ", \"icon\": " << e.icon;
            if (e.radius)
                o << ", \"radius\": " << e.radius;
            o << ", \"stroke\": " << e.stroke;
            if (!e.visible)
                o << ", \"visible\": false";
            if (e.locked)
                o << ", \"locked\": true";
            if (!e.enabled)
                o << ", \"enabled\": false";
            if (!e.points.empty()) {
                o << ", \"points\": [";
                for (size_t k = 0; k < e.points.size(); k++) {
                    if (k)
                        o << ", ";
                    o << "[" << e.points[k].x << ", " << e.points[k].y << "]";
                }
                o << "], \"closed\": " << (e.closed ? "true" : "false");
            }
            if (e.type == "image" || e.type == "video" || e.type == "overlay")
                o << ", \"uv\": [" << e.uv.x << ", " << e.uv.y << ", " << e.uv.w << ", " << e.uv.h << "]";
            o << "}" << (j + 1 < s.elements.size() ? "," : "") << "\n";
        }
        o << "     ]}" << (i + 1 < doc.screens.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}
LayoutDocument loadLayout(const std::string &path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || in.tellg() > 8 * 1024 * 1024)
        fail("cannot read file, or file exceeds 8 MiB");
    in.seekg(0);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    return parseLayout(text);
}
void saveLayout(const std::string &path, const LayoutDocument &doc) {
    const auto text = serializeLayout(doc);
    const auto target = std::filesystem::u8path(path), temp = std::filesystem::u8path(path + ".cyvr.tmp");
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    std::error_code error;
    if (out)
        std::filesystem::rename(temp, target, error);
    if (!out || error) {
        std::filesystem::remove(temp, error);
        fail("could not save file");
    }
}
} // namespace cyvr
