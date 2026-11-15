#include "player.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

#include "stb_image_write.h"
#include "texture.hh"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace splash {

namespace {

std::string utf8(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

PlayerOs hostOs() {
#if defined(_WIN32)
    return PlayerOs::Windows;
#elif defined(__APPLE__)
    return PlayerOs::Mac;
#else
    return PlayerOs::Linux;
#endif
}

// Whether `inner` is `outer` or inside it, both made absolute and resolved as far as they exist.
bool within(const fs::path& inner, const fs::path& outer) {
    std::error_code ec;
    const fs::path i = fs::weakly_canonical(fs::absolute(inner, ec), ec);
    const fs::path o = fs::weakly_canonical(fs::absolute(outer, ec), ec);
    const fs::path rel = i.lexically_relative(o);
    return !rel.empty() && *rel.begin() != "..";
}

// `s` inside "..." of a POSIX shell: \ $ ` and " escaped.
std::string shQuoted(const std::string& s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '\\' || c == '$' || c == '`' || c == '"') q += '\\';
        q += c;
    }
    return q + "\"";
}

// `rel` ('/' separators) under %~dp0 inside "..." of a .bat: backslashes, % doubled.
std::string batQuoted(const std::string& rel) {
    std::string q = "\"%~dp0";
    for (char c : rel) {
        if (c == '/') q += '\\';
        else if (c == '%') q += "%%";
        else q += c;
    }
    return q + "\"";
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    if (!f) throw std::runtime_error("cannot write " + utf8(file));
}

void makeExecutable(const fs::path& file) {
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add,
                    ec);
}

// An AppImage starts as an ELF with "AI" 2 at offset 8.
bool isAppImage(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    char head[11] = {};
    f.read(head, sizeof head);
    return f && head[0] == 0x7f && head[1] == 'E' && head[8] == 'A' && head[9] == 'I' && head[10] == 2;
}

// Texel (x, y) of `ic` drawn `n` pixels square, 0xAABBGGRR: a 16x16 icon
// (the memory card's) nearest-neighbour, a bigger one averaged down (colour
// weighted by alpha) or nearest up.
uint32_t iconTexel(const ExeIcon& ic, int n, int x, int y) {
    const int s = ic.size;
    if (s == 16 || s <= n) return ic.rgba[size_t(y * s / n) * size_t(s) + size_t(x * s / n)];
    // The source texels under this one, colour weighted by alpha.
    const int x0 = x * s / n, x1 = std::max(x0 + 1, (x + 1) * s / n);
    const int y0 = y * s / n, y1 = std::max(y0 + 1, (y + 1) * s / n);
    uint64_t r = 0, g = 0, b = 0, a = 0;
    for (int sy = y0; sy < y1; ++sy)
        for (int sx = x0; sx < x1; ++sx) {
            const uint32_t c = ic.rgba[size_t(sy) * size_t(s) + size_t(sx)];
            const uint32_t ca = c >> 24;
            r += (c & 0xff) * ca;
            g += ((c >> 8) & 0xff) * ca;
            b += ((c >> 16) & 0xff) * ca;
            a += ca;
        }
    const uint64_t count = uint64_t(x1 - x0) * uint64_t(y1 - y0);
    if (!a) return 0;
    return uint32_t(r / a) | uint32_t(g / a) << 8 | uint32_t(b / a) << 16 | uint32_t(a / count) << 24;
}

// `ic` as a PNG, RGBA (colour type 6, what pcsx-redux's gui.WindowIcon
// decodes), 256 pixels square or its own size when smaller.
void writeIconPng(const ExeIcon& ic, const fs::path& file) {
    const int n = std::min(ic.size, 256);
    std::vector<uint8_t> rgba;
    rgba.reserve(size_t(n) * size_t(n) * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint32_t c = iconTexel(ic, n, x, y);
            for (int k = 0; k < 4; ++k) rgba.push_back(uint8_t(c >> (8 * k)));
        }
    std::string png;
    auto put = [](void* to, void* data, int size) { static_cast<std::string*>(to)->append(static_cast<const char*>(data), size_t(size)); };
    if (!stbi_write_png_to_func(put, &png, n, n, 4, rgba.data(), n * 4)) throw std::runtime_error("cannot encode " + utf8(file));
    writeText(file, png);
}

}  // namespace

bool findReduxInstall(const fs::path& redux, ReduxInstall& out, std::string& error) {
    std::error_code ec;
    if (redux.empty() || !fs::is_regular_file(redux, ec)) {
        error = redux.empty() ? "no pcsx-redux is set (Build > Tools...)" : "pcsx-redux not found: " + utf8(redux);
        return false;
    }
    out = ReduxInstall();
    // An AppDir's AppRun is often a symlink itself (to usr/bin/pcsx-redux), so
    // the links are followed one at a time and the first AppRun on the way wins.
    fs::path p = fs::absolute(redux, ec);
    for (int hop = 0; hop < 32; ++hop) {
        if (p.filename() == "AppRun") {
            out.kind = ReduxKind::AppDir;
            out.root = p.parent_path();
            out.exe = utf8(out.root.filename()) + "/AppRun";
            out.os = PlayerOs::Linux;
            return true;
        }
        if (!fs::is_symlink(p, ec)) break;
        fs::path target = fs::read_symlink(p, ec);
        if (ec) break;
        p = (target.is_absolute() ? target : p.parent_path() / target).lexically_normal();
    }
    fs::path real = fs::canonical(redux, ec);
    if (ec) real = fs::absolute(redux, ec);
    for (fs::path a = real.parent_path(); a.has_relative_path(); a = a.parent_path()) {
        if (lower(utf8(a.extension())) == ".app") {
            out.kind = ReduxKind::App;
            out.root = a;
            const std::u8string rel = real.lexically_relative(a).generic_u8string();
            out.exe = utf8(a.filename()) + "/" + std::string(rel.begin(), rel.end());
            out.os = PlayerOs::Mac;
            return true;
        }
        if (a == a.parent_path()) break;
    }
    const std::string name = utf8(real.filename());
    if (lower(utf8(real.extension())) == ".exe" || name == "pcsx-redux.main") {
        out.kind = ReduxKind::Folder;
        out.root = real.parent_path();
        // pcsx-redux.main is what pcsx-redux.exe starts (editor::withDefaults
        // runs it directly); a launcher starts the .exe beside it, as a user would.
        out.exe = name == "pcsx-redux.main" && fs::is_regular_file(out.root / "pcsx-redux.exe", ec) ? "pcsx-redux.exe" : name;
        out.os = PlayerOs::Windows;
        return true;
    }
    out.kind = ReduxKind::File;
    out.root = real;
    out.exe = name;
    out.os = hostOs();
    return true;
}

std::string playerFileName(const std::string& title, bool ascii) {
    std::string s;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || (ascii && u > 0x7e)) continue;
        if (std::string_view("<>:\"/\\|?*%$`!").find(c) != std::string_view::npos) continue;
        s += c;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '.')) ++start;
    s = s.substr(start);
    if (s.empty()) return "Game";
    static const char* const reserved[] = {"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                           "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                           "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string stem = lower(s.substr(0, s.find('.')));
    for (const char* r : reserved)
        if (stem == r) return s + " Game";
    return s;
}

std::string launcherName(const std::string& title, PlayerOs os) {
    return playerFileName(title) + (os == PlayerOs::Windows ? ".bat" : os == PlayerOs::Mac ? ".command" : ".sh");
}

std::string launcherText(PlayerOs os, const std::string& exe, const std::string& cue, const std::string& config) {
    const std::string flags = " -kiosk -run -iso ";
    if (os == PlayerOs::Windows)
        return "@echo off\r\n"
               "rem Starts the game in pcsx-redux, from wherever this folder is. Written by SplashEdit.\r\n"
               "start \"\" " +
               batQuoted("redux/" + exe) + flags + batQuoted(cue) + " -portable " + batQuoted(config) + "\r\n";
    return "#!/bin/sh\n"
           "# Starts the game in pcsx-redux, from wherever this folder is. Written by SplashEdit.\n"
           "here=$(cd \"$(dirname \"$0\")\" && pwd)\n"
           "exec \"$here\"/" +
           shQuoted("redux/" + exe) + flags + "\"$here\"/" + shQuoted(cue) + " -portable \"$here\"/" + shQuoted(config) +
           "\n";
}

std::string launchConfig(const std::string& exe, const std::string& cue, const std::string& config) {
    return "redux/" + exe + "\n" + cue + "\n" + config + "\n";
}

#ifdef _WIN32
namespace {

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

// One RT_ICON image, `n` pixels square: a BITMAPINFOHEADER (the height
// doubled for the mask), 32-bit BGRA rows bottom up, then the 1-bit AND mask.
// A 16x16 icon (the memory card's) is scaled nearest-neighbour, alpha below
// 128 transparent and the rest opaque; a bigger one as iconTexel scales it,
// its alpha kept.
std::vector<uint8_t> iconImage(const ExeIcon& ic, int n) {
    const bool hard = ic.size == 16;
    auto at = [&](int x, int y) { return iconTexel(ic, n, x, y); };
    std::vector<uint8_t> b;
    put32(b, 40);
    put32(b, uint32_t(n));
    put32(b, uint32_t(2 * n));
    put16(b, 1);
    put16(b, 32);
    for (int i = 0; i < 6; ++i) put32(b, 0);  // BI_RGB, no size, resolution or palette
    for (int y = n - 1; y >= 0; --y)
        for (int x = 0; x < n; ++x) {
            const uint32_t c = at(x, y);
            const uint32_t a = hard ? ((c >> 24) >= 128 ? 255 : 0) : c >> 24;
            b.push_back(a ? uint8_t(c >> 16) : 0);
            b.push_back(a ? uint8_t(c >> 8) : 0);
            b.push_back(a ? uint8_t(c) : 0);
            b.push_back(uint8_t(a));
        }
    const size_t stride = size_t(n + 31) / 32 * 4;
    for (int y = n - 1; y >= 0; --y) {
        std::vector<uint8_t> row(stride, 0);
        for (int x = 0; x < n; ++x)
            if ((at(x, y) >> 24) < 128) row[size_t(x / 8)] |= uint8_t(0x80 >> (x % 8));
        b.insert(b.end(), row.begin(), row.end());
    }
    return b;
}

// RT_ICON, RT_GROUP_ICON and RT_VERSION for the W functions (windows.h gives them as char*).
const LPCWSTR kRtIcon = MAKEINTRESOURCEW(3), kRtGroupIcon = MAKEINTRESOURCEW(14), kRtVersion = MAKEINTRESOURCEW(16);

// A resource id: a number, or a name.
struct ResId {
    WORD id = 0;
    std::wstring name;
    explicit ResId(LPCWSTR s) {
        if (IS_INTRESOURCE(s)) id = WORD(reinterpret_cast<ULONG_PTR>(s));
        else name = s;
    }
    LPCWSTR get() const { return name.empty() ? MAKEINTRESOURCEW(id) : name.c_str(); }
};
// The resources a program keeps when its icons or details are replaced: all
// but those (`icons`, `version` say which go).
struct Kept {
    ResId type, name;
    WORD lang;
    std::vector<uint8_t> data;
};
struct KeptList {
    bool icons = false, version = false;
    std::vector<Kept> kept;
};
BOOL CALLBACK keptLanguage(HMODULE m, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR p) {
    HRSRC r = FindResourceExW(m, type, name, lang);
    const auto* d = r ? static_cast<const uint8_t*>(LockResource(LoadResource(m, r))) : nullptr;
    if (d) reinterpret_cast<KeptList*>(p)->kept.push_back({ResId(type), ResId(name), lang, {d, d + SizeofResource(m, r)}});
    return TRUE;
}
BOOL CALLBACK keptName(HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR p) {
    EnumResourceLanguagesW(m, type, name, keptLanguage, p);
    return TRUE;
}
BOOL CALLBACK keptType(HMODULE m, LPWSTR type, LONG_PTR p) {
    const KeptList& k = *reinterpret_cast<KeptList*>(p);
    const bool goes = (k.icons && (type == kRtIcon || type == kRtGroupIcon)) || (k.version && type == kRtVersion);
    if (!goes) EnumResourceNamesW(m, type, keptName, p);
    return TRUE;
}

}  // namespace
#endif

bool setExeIcon(const fs::path& exe, const std::vector<uint32_t>& rgba16, std::string& error) {
    if (rgba16.size() != size_t(16 * 16)) {
        error = "the icon is not 16x16";
        return false;
    }
    const ExeIcon icon{16, rgba16};
    return setExeResources(exe, &icon, nullptr, error);
}

bool setExeResources(const fs::path& exe, const ExeIcon* icon, const ExeDetails* details, std::string& error) {
    error.clear();
    if (icon && (icon->size < 1 || icon->size > 4096 || icon->rgba.size() != size_t(icon->size) * size_t(icon->size))) {
        error = "the icon is not a square image";
        return false;
    }
#ifdef _WIN32
    // Every resource goes and all but the ones replaced come back, so none of
    // the stub's icons is left behind ours (deleting them one by one through
    // UpdateResourceW does nothing under Wine).
    KeptList kept;
    kept.icons = icon != nullptr;
    kept.version = details != nullptr;
    if (HMODULE m = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE)) {
        EnumResourceTypesW(m, keptType, reinterpret_cast<LONG_PTR>(&kept));
        FreeLibrary(m);
    } else {
        error = "cannot read " + utf8(exe) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    HANDLE h = BeginUpdateResourceW(exe.c_str(), TRUE);
    if (!h) {
        error = "cannot change " + utf8(exe) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    bool ok = true;
    for (Kept& k : kept.kept)
        ok = ok && UpdateResourceW(h, k.type.get(), k.name.get(), k.lang, k.data.data(), DWORD(k.data.size()));
    if (icon) {
        const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
        const std::vector<int> sizes = icon->size == 16 ? std::vector<int>{16, 32, 48} : std::vector<int>{16, 32, 48, 256};
        // GRPICONDIR then a GRPICONDIRENTRY per image, naming its RT_ICON id.
        std::vector<uint8_t> group;
        put16(group, 0);
        put16(group, 1);
        put16(group, uint32_t(sizes.size()));
        WORD id = 1;
        for (int n : sizes) {
            std::vector<uint8_t> image = iconImage(*icon, n);
            ok = ok && UpdateResourceW(h, kRtIcon, MAKEINTRESOURCEW(id), lang, image.data(), DWORD(image.size()));
            group.push_back(uint8_t(n & 0xff));  // 256 is 0
            group.push_back(uint8_t(n & 0xff));
            group.push_back(0);
            group.push_back(0);
            put16(group, 1);
            put16(group, 32);
            put32(group, uint32_t(image.size()));
            put16(group, id++);
        }
        ok = ok && UpdateResourceW(h, kRtGroupIcon, MAKEINTRESOURCEW(1), lang, group.data(), DWORD(group.size()));
    }
    if (details) {
        std::vector<uint8_t> v = versionResource(*details, utf8(exe.filename()));
        ok = ok && UpdateResourceW(h, kRtVersion, MAKEINTRESOURCEW(1), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), v.data(),
                                   DWORD(v.size()));
    }
    const DWORD failed = ok ? 0 : GetLastError();
    if (!EndUpdateResourceW(h, ok ? FALSE : TRUE) || !ok) {
        error = "cannot set the icon and details of " + utf8(exe) + " (error " + std::to_string(ok ? GetLastError() : failed) +
                ")";
        return false;
    }
    return true;
#else
    (void)exe;
    (void)details;
    error = "a program's icon and details are set on Windows only";
    return false;
#endif
}

namespace {

// UTF-8 as UTF-16; a bad byte reads as U+FFFD.
std::u16string utf16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xfffd;
        size_t len = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6) cp = c & 0x1f, len = 2;
        else if ((c >> 4) == 14) cp = c & 0x0f, len = 3;
        else if ((c >> 3) == 30) cp = c & 0x07, len = 4;
        if (len > 1) {
            if (i + len > s.size()) len = 1, cp = 0xfffd;
            for (size_t k = 1; k < len; ++k) {
                const unsigned char d = static_cast<unsigned char>(s[i + k]);
                if ((d >> 6) != 2) {
                    len = k;
                    cp = 0xfffd;
                    break;
                }
                cp = cp << 6 | (d & 0x3f);
            }
        }
        i += len;
        if (cp >= 0x10000 && cp <= 0x10ffff) {
            cp -= 0x10000;
            out += char16_t(0xd800 + (cp >> 10));
            out += char16_t(0xdc00 + (cp & 0x3ff));
        } else {
            out += char16_t(cp > 0x10ffff ? 0xfffd : cp);
        }
    }
    return out;
}

// VS_VERSIONINFO's blocks: wLength, wValueLength, wType, the key (UTF-16,
// NUL-ended), padding to 32 bits, the value, the children; each block starts
// 32-bit aligned.
struct VersionWriter {
    std::vector<uint8_t> b;
    void align() {
        while (b.size() % 4) b.push_back(0);
    }
    void w16(uint32_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    }
    void w32(uint32_t v) {
        w16(v & 0xffff);
        w16(v >> 16);
    }
    void wstr(const std::u16string& s) {
        for (char16_t c : s) w16(c);
        w16(0);
    }
    size_t begin(const std::u16string& key, uint32_t valueLength, uint32_t type) {
        align();
        const size_t at = b.size();
        w16(0);
        w16(valueLength);
        w16(type);
        wstr(key);
        align();
        return at;
    }
    void end(size_t at) {
        const size_t len = b.size() - at;
        b[at] = uint8_t(len);
        b[at + 1] = uint8_t(len >> 8);
    }
};

// "1.2.3" as two DWORDs, 1.2 and 3.0; zeros when it is not up to four numbers below 65536.
void versionNumbers(const std::string& v, uint32_t& ms, uint32_t& ls) {
    ms = ls = 0;
    uint32_t n[4] = {0, 0, 0, 0};
    size_t i = 0, part = 0;
    if (v.empty()) return;
    while (true) {
        if (i >= v.size() || !std::isdigit(static_cast<unsigned char>(v[i]))) return;
        uint32_t x = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
            x = x * 10 + uint32_t(v[i] - '0');
            if (x > 0xffff) return;
            ++i;
        }
        n[part++] = x;
        if (i == v.size()) break;
        if (v[i] != '.' || part == 4) return;
        ++i;
    }
    ms = n[0] << 16 | n[1];
    ls = n[2] << 16 | n[3];
}

}  // namespace

std::vector<uint8_t> versionResource(const ExeDetails& d, const std::string& exe) {
    VersionWriter w;
    uint32_t ms, ls;
    versionNumbers(d.version, ms, ls);
    const size_t root = w.begin(u"VS_VERSION_INFO", 52, 0);
    // VS_FIXEDFILEINFO: the signature, version 1.0, the file and product
    // versions, all flags valid and none set, VOS_NT_WINDOWS32, VFT_APP, no date.
    for (uint32_t v : {0xfeef04bdu, 0x00010000u, ms, ls, ms, ls, 0x3fu, 0u, 0x00040004u, 1u, 0u, 0u, 0u}) w.w32(v);
    const size_t sfi = w.begin(u"StringFileInfo", 0, 1);
    const size_t table = w.begin(u"040904b0", 0, 1);  // US English, Unicode
    const std::pair<const char16_t*, std::string> strings[] = {
        {u"CompanyName", d.company},  {u"FileDescription", d.description}, {u"FileVersion", d.version},
        {u"InternalName", exe},       {u"LegalCopyright", d.copyright},    {u"OriginalFilename", exe},
        {u"ProductName", d.product},  {u"ProductVersion", d.version},
    };
    for (const auto& [key, value] : strings) {
        if (value.empty()) continue;
        const std::u16string v = utf16(value);
        const size_t at = w.begin(key, uint32_t(v.size() + 1), 1);  // in WCHARs, the NUL counted
        w.wstr(v);
        w.end(at);
    }
    w.end(table);
    w.end(sfi);
    const size_t vfi = w.begin(u"VarFileInfo", 0, 1);
    const size_t var = w.begin(u"Translation", 4, 0);
    w.w16(0x0409);
    w.w16(1200);
    w.end(var);
    w.end(vfi);
    w.end(root);
    return w.b;
}

ExeIcon squareIcon(const std::vector<uint32_t>& rgba, int w, int h) {
    ExeIcon ic;
    if (w <= 0 || h <= 0 || rgba.size() != size_t(w) * size_t(h)) return ic;
    ic.size = std::max(w, h);
    ic.rgba.assign(size_t(ic.size) * size_t(ic.size), 0);
    const int ox = (ic.size - w) / 2, oy = (ic.size - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) ic.rgba[size_t(y + oy) * size_t(ic.size) + size_t(x + ox)] = rgba[size_t(y) * size_t(w) + size_t(x)];
    return ic;
}

static int clampScale(int s) { return s >= 1 && s <= 4 ? s : 2; }

std::string reduxConfig(const PlayerOptions& opt, std::string& error) {
    error.clear();
    const int scale = clampScale(opt.windowScale);
    nlohmann::json j = {
        {"emulator",
         {{"ShownAutoUpdateConfig", true}, {"AutoUpdate", false}, {"LinearFiltering", opt.linearFiltering}, {"FastBoot", opt.fastBoot}}},
        {"gui", {{"WindowSizeX", 320 * scale}, {"WindowSizeY", 240 * scale}}},
    };
    if (!opt.windowTitle.empty()) j["gui"]["WindowTitle"] = opt.windowTitle;
    if (opt.windowIcon.size) j["gui"]["WindowIcon"] = kWindowIconFile;
    if (!opt.reduxConfig.empty()) {
        const nlohmann::json own = nlohmann::json::parse(opt.reduxConfig, nullptr, false);
        if (!own.is_object()) {
            error = own.is_discarded() ? "it is not valid JSON" : "it is not a JSON object";
            return {};
        }
        j.merge_patch(own);
    }
    return j.dump(1) + "\n";
}

std::string seededReduxConfig() {
    std::string error;
    return reduxConfig(PlayerOptions(), error);
}

PlayerSettings readPlayerSettings(const fs::path& file) {
    PlayerSettings s;
    std::ifstream in(file, std::ios::binary);
    if (!in) return s;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return s;
    auto str = [&](const char* key, std::string& to) {
        if (auto it = j.find(key); it != j.end() && it->is_string()) to = it->get<std::string>();
    };
    str("name", s.name);
    str("exe", s.exe);
    str("icon", s.icon);
    str("company", s.company);
    str("version", s.version);
    str("copyright", s.copyright);
    str("pcsxJson", s.pcsxJson);
    if (auto it = j.find("linearFiltering"); it != j.end() && it->is_boolean()) s.linearFiltering = it->get<bool>();
    if (auto it = j.find("fastBoot"); it != j.end() && it->is_boolean()) s.fastBoot = it->get<bool>();
    if (auto it = j.find("windowScale"); it != j.end() && it->is_number_integer()) s.windowScale = clampScale(it->get<int>());
    return s;
}

bool writePlayerSettings(const fs::path& file, const PlayerSettings& s) {
    nlohmann::json j = nlohmann::json::object();
    auto str = [&](const char* key, const std::string& v) {
        if (!v.empty()) j[key] = v;
    };
    str("name", s.name);
    str("exe", s.exe);
    str("icon", s.icon);
    str("company", s.company);
    str("version", s.version);
    str("copyright", s.copyright);
    str("pcsxJson", s.pcsxJson);
    if (s.linearFiltering) j["linearFiltering"] = true;
    if (!s.fastBoot) j["fastBoot"] = false;
    if (clampScale(s.windowScale) != 2) j["windowScale"] = clampScale(s.windowScale);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << j.dump(2) << "\n";
    out.close();
    return !out.fail();
}

std::string playerOptions(const PlayerSettings& s, const fs::path& projectRoot, const std::string& title, PlayerOptions& out) {
    out = PlayerOptions();
    out.exeName = s.exe;
    out.details.product = title;
    out.details.description = title;
    out.details.company = s.company;
    out.details.version = s.version;
    out.details.copyright = s.copyright;
    out.linearFiltering = s.linearFiltering;
    out.fastBoot = s.fastBoot;
    out.windowScale = clampScale(s.windowScale);
    out.windowTitle = title;
    auto resolve = [&](const std::string& p) {
        const fs::path f = fromUtf8(p);
        return f.is_absolute() ? f : projectRoot / f;
    };
    std::string error;
    if (!s.icon.empty()) {
        try {
            const Image img = loadImage(resolve(s.icon));
            std::vector<uint32_t> rgba(size_t(img.width) * size_t(img.height));
            auto byte = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    // Image rows are bottom up; the icon's top down.
                    const Image::Px& p = img.pixels[size_t(img.height - 1 - y) * size_t(img.width) + size_t(x)];
                    rgba[size_t(y) * size_t(img.width) + size_t(x)] = byte(p.r) | byte(p.g) << 8 | byte(p.b) << 16 | byte(p.a) << 24;
                }
            out.icon = squareIcon(rgba, img.width, img.height);
            out.windowIcon = out.icon;
        } catch (const std::exception& e) {
            error = "the player's icon " + s.icon + " cannot be read: " + e.what();
        }
    }
    if (!s.pcsxJson.empty()) {
        std::ifstream in(resolve(s.pcsxJson), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), {});
        std::string e;
        if (text.empty()) e = "it cannot be read or is empty";
        else {
            PlayerOptions probe;
            probe.reduxConfig = text;
            reduxConfig(probe, e);
        }
        if (!e.empty()) {
            if (error.empty()) error = "the player's pcsx.json " + s.pcsxJson + " is not used: " + e;
            else error += "; the player's pcsx.json " + s.pcsxJson + " is not used: " + e;
        } else {
            out.reduxConfig = std::move(text);
        }
    }
    return error;
}

std::string exportDirError(const fs::path& out, const char* marker, const std::string& what) {
    std::error_code ec;
    if (!fs::exists(out, ec)) return {};
    if (!fs::is_directory(out, ec)) return utf8(out) + " is a file, not a folder";
    if (fs::is_regular_file(out / marker, ec)) return {};
    fs::directory_iterator it(out, ec);
    if (ec) return "cannot read " + utf8(out) + ": " + ec.message();
    if (it != fs::directory_iterator()) return utf8(out) + " is not empty and holds no " + what + ": pick an empty folder";
    return {};
}

std::string playerDirError(const fs::path& out) { return exportDirError(out, kPlayerMarker, "standalone player"); }

void removeExported(const fs::path& out, const char* marker) {
    std::ifstream m(out / marker);
    std::string line;
    while (m && std::getline(m, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const fs::path name = fromUtf8(line);
        if (line.empty() || line == "." || line == ".." || name.has_parent_path() || name.has_root_path()) continue;
        fs::remove_all(out / name);
    }
}

PlayerResult writePlayer(const fs::path& bin, const fs::path& redux, const fs::path& out, const std::string& title,
                         const fs::path& launcherStub, const std::vector<uint32_t>* icon) {
    PlayerOptions opt;
    if (icon) opt.icon = {16, *icon};
    return writePlayer(bin, redux, out, title, launcherStub, opt);
}

PlayerResult writePlayer(const fs::path& bin, const fs::path& redux, const fs::path& out, const std::string& title,
                         const fs::path& launcherStub, const PlayerOptions& opt) {
    PlayerResult r;
    ReduxInstall ri;
    if (!findReduxInstall(redux, ri, r.error)) return r;
    r.os = ri.os;
    std::error_code ec;
    fs::path cue = bin;
    cue.replace_extension(".cue");
    if (!fs::is_regular_file(bin, ec) || !fs::is_regular_file(cue, ec)) {
        r.error = "disc image not found: " + utf8(bin);
        return r;
    }
    if (std::string e = playerDirError(out); !e.empty()) {
        r.error = e;
        return r;
    }
    if (within(out, ri.root)) {
        r.error = "the player folder " + utf8(out) + " is inside the pcsx-redux it copies (" + utf8(ri.root) +
                  "): pick a folder outside it";
        return r;
    }
    if (within(bin, out)) {
        r.error = "the disc image " + utf8(bin) + " is inside the player folder " + utf8(out);
        return r;
    }
    std::string configError;
    const std::string config = reduxConfig(opt, configError);
    if (!configError.empty()) {
        r.error = "the project's pcsx.json is not used: " + configError;
        return r;
    }
    try {
        fs::create_directories(out);
        // What a previous export wrote goes; whatever else is there stays.
        removeExported(out, kPlayerMarker);
        fs::create_directories(out / "game");
        fs::copy_file(bin, out / "game" / bin.filename(), fs::copy_options::overwrite_existing);
        fs::copy_file(cue, out / "game" / cue.filename(), fs::copy_options::overwrite_existing);

        const auto how = fs::copy_options::recursive | fs::copy_options::copy_symlinks;
        if (ri.kind == ReduxKind::Folder) {
            fs::copy(ri.root, out / "redux", how);
        } else {
            fs::create_directories(out / "redux");
            fs::copy(ri.root, out / "redux" / ri.root.filename(), how);
        }
        if (ri.os != PlayerOs::Windows) makeExecutable(out / "redux" / fromUtf8(ri.exe));
        if (ri.kind == ReduxKind::File && ri.os == PlayerOs::Linux && isAppImage(ri.root))
            r.warnings.push_back("pcsx-redux is an AppImage: some systems need FUSE installed to start it");
        else if (ri.kind == ReduxKind::File)
            r.warnings.push_back("pcsx-redux was copied on its own (" + utf8(ri.root) +
                                 "): the libraries it uses must be installed where the player runs");

        fs::create_directories(out / "config");
        writeText(out / "config" / "pcsx.json", config);
        if (opt.windowIcon.size) writeIconPng(opt.windowIcon, out / "config" / kWindowIconFile);

        const std::string cueRel = "game/" + utf8(cue.filename());
        const bool stub = ri.os == PlayerOs::Windows && !launcherStub.empty() && fs::is_regular_file(launcherStub, ec);
        if (ri.os == PlayerOs::Windows && !launcherStub.empty() && !stub)
            r.warnings.push_back("the player launcher " + utf8(launcherStub) +
                                 " is missing: the player starts from a .bat instead, without the game's icon");
        const std::string name = opt.exeName.empty() ? title : opt.exeName;
        const std::string launcher = stub ? playerFileName(name) + ".exe" : launcherName(name, ri.os);
        r.launcher = out / fromUtf8(launcher);
        if (stub) {
            fs::copy_file(launcherStub, r.launcher, fs::copy_options::overwrite_existing);
            writeText(out / "config" / "launch.txt", launchConfig(ri.exe, cueRel, "config"));
            const ExeDetails& d = opt.details;
            const bool details = !(d.description.empty() && d.product.empty() && d.company.empty() && d.version.empty() &&
                                   d.copyright.empty());
            if (std::string e; (opt.icon.size || details) &&
                               !setExeResources(r.launcher, opt.icon.size ? &opt.icon : nullptr, details ? &d : nullptr, e))
                r.warnings.push_back(std::string("the player keeps the default ") + (opt.icon.size ? "icon" : "details") +
                                     ": " + e);
        } else {
            writeText(r.launcher, launcherText(ri.os, ri.exe, cueRel, "config"));
        }
        if (ri.os != PlayerOs::Windows) makeExecutable(r.launcher);
        writeText(out / kPlayerMarker, "game\nredux\nconfig\n" + launcher + "\n");
    } catch (const std::exception& e) {
        r.error = e.what();
        r.launcher.clear();
    }
    return r;
}

}  // namespace splash
