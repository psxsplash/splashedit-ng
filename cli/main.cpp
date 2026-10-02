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

#include "splashpack.hh"

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
                 "  --project defaults to the directory holding the scene file\n"
                 "  --order-from (parity tests) orders objects like the name table of ref\n");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "export") != 0) return usage();
    std::string scenePath, outPath, project, orderFrom;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)
            outPath = argv[++i];
        else if (a == "--project" && i + 1 < argc)
            project = argv[++i];
        else if (a == "--order-from" && i + 1 < argc)
            orderFrom = argv[++i];
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
        splash::ExportResult r = splash::exportSplashpack(scene, root, outPath, opt);
        for (auto& m : r.warnings) std::fprintf(stderr, "warning: %s\n", m.c_str());
        for (auto& m : r.errors) std::fprintf(stderr, "error: %s\n", m.c_str());
        return r.ok() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
