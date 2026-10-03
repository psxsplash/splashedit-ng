// splashpack-cli: export a scene file to a splashpack without the editor.
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <string>

#include "luacompile.hh"
#include "splashpack.hh"
#include "texture.hh"

namespace fs = std::filesystem;

// Object names from a splashpack's name table (header: object count at 6,
// name table offset at 60).
static std::vector<std::string> readNameTable(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 144 || d[0] != 'S' || d[1] != 'P') throw std::runtime_error(path + ": not a splashpack");
    unsigned count = d[6] | (d[7] << 8);
    size_t p = d[60] | (d[61] << 8) | (d[62] << 16) | (size_t(d[63]) << 24);
    std::vector<std::string> names;
    for (unsigned i = 0; i < count && p < d.size(); i++) {
        unsigned n = d[p];
        names.emplace_back(reinterpret_cast<const char*>(&d[p + 1]), n);
        p += n + 2;
    }
    return names;
}

static int usage() {
    std::fprintf(stderr,
                 "usage: splashpack-cli export <scene> -o <out.splashpack> [--project <dir>] [--order-from <ref.splashpack>]\n"
                 "                              [--lua-bytecode]\n"
                 "  --project defaults to the directory holding the scene file\n"
                 "  --lua-bytecode stores Lua scripts compiled (as luac_psx would) instead of as source\n"
                 "  --order-from (parity tests) orders objects like the name table of ref\n"
                 "usage: splashpack-cli texstats <image> [--bpp 4|8|16] [--cutout] [--out <decoded.png>]\n"
                 "  converts one image the way export does and prints its error against the source\n"
                 "usage: splashpack-cli resave <in.scene|in.mesh> <out>\n"
                 "  loads and saves a scene or mesh file\n"
                 "usage: splashpack-cli luac <in.lua> -o <out.luac>\n"
                 "  compiles one Lua file to PS1 bytecode, as --lua-bytecode does\n");
    return 2;
}

static int texstats(int argc, char** argv) {
    std::string path;
    splash::BitDepth depth = splash::BitDepth::Bpp8;
    bool cutout = false;
    std::string outPng;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--bpp" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "4") depth = splash::BitDepth::Bpp4;
            else if (v == "8") depth = splash::BitDepth::Bpp8;
            else if (v == "16") depth = splash::BitDepth::Bpp16;
            else return usage();
        } else if (a == "--out" && i + 1 < argc) {
            outPng = argv[++i];
        } else if (a == "--cutout") {
            cutout = true;
        } else if (path.empty()) {
            path = a;
        } else {
            return usage();
        }
    }
    if (path.empty()) return usage();
    try {
        splash::Image img = splash::loadImage(path);
        splash::PsxTexture t = splash::convertTexture(img, depth, cutout);
        splash::TextureError e = splash::measureTexture(img, t);
        if (!outPng.empty()) splash::savePng(splash::decodeTexture(t), outPng);
        std::printf("%s bpp=%d psnr=%.2f deltaE=%.3f deltaE_blur=%.3f colors=%d\n", path.c_str(), int(depth), e.psnr,
                    e.deltaE, e.deltaEBlur, e.colorsUsed);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

static int resave(int argc, char** argv) {
    if (argc != 4) return usage();
    try {
        fs::path in = argv[2];
        if (in.extension() == ".mesh")
            splash::saveMesh(splash::loadMesh(in), argv[3]);
        else
            splash::saveScene(splash::loadScene(in), argv[3]);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

static int luac(int argc, char** argv) {
    if (argc != 5 || std::strcmp(argv[3], "-o") != 0) return usage();
    try {
        std::ifstream in(argv[2], std::ios::binary);
        if (!in) throw std::runtime_error(std::string("cannot open ") + argv[2]);
        std::string src((std::istreambuf_iterator<char>(in)), {});
        std::vector<uint8_t> bc = splash::compileLua(src, fs::path(argv[2]).filename().string());
        std::ofstream out(argv[4], std::ios::binary);
        out.write(reinterpret_cast<const char*>(bc.data()), std::streamsize(bc.size()));
        if (!out) throw std::runtime_error(std::string("cannot write ") + argv[4]);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "texstats") == 0) return texstats(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "resave") == 0) return resave(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "luac") == 0) return luac(argc, argv);
    if (argc < 2 || std::strcmp(argv[1], "export") != 0) return usage();
    std::string scenePath, outPath, project, orderFrom;
    bool luaBytecode = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)
            outPath = argv[++i];
        else if (a == "--project" && i + 1 < argc)
            project = argv[++i];
        else if (a == "--order-from" && i + 1 < argc)
            orderFrom = argv[++i];
        else if (a == "--lua-bytecode")
            luaBytecode = true;
        else if (scenePath.empty())
            scenePath = a;
        else
            return usage();
    }
    if (scenePath.empty() || outPath.empty()) return usage();
    try {
        splash::Scene scene = splash::loadScene(scenePath);
        fs::path root = project.empty() ? fs::path(scenePath).parent_path() : fs::path(project);
        splash::ExportOptions opt;
        if (!orderFrom.empty()) opt.objectOrder = readNameTable(orderFrom);
        opt.luaBytecode = luaBytecode;
        splash::ExportResult r = splash::exportSplashpack(scene, root, outPath, opt);
        for (auto& m : r.warnings) std::fprintf(stderr, "warning: %s\n", m.c_str());
        for (auto& m : r.errors) std::fprintf(stderr, "error: %s\n", m.c_str());
        return r.ok() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
