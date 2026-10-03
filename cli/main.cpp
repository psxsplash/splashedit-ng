// splashpack-cli: export a scene file to a splashpack without the editor.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <string>

#include "luacompile.hh"
#include "audio.hh"
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
                 "usage: splashpack-cli audio <in.wav> -o <out.adpcm> [--rate <hz>] [--loop] [--trim]\n"
                 "    [--pcm <src.wav>] [--decoded <out.wav>]\n"
                 "  encodes one clip the way export does and prints its SNR against the source;\n"
                 "  --pcm writes the 16-bit mono WAV the encoder starts from, before resampling\n"
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

static void writeWav16(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = uint32_t(pcm.size() * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
}

static int audio(int argc, char** argv) {
    std::string in, out, pcmOut, decodedOut;
    int rate = 22050;
    bool loop = false, trim = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--rate" && i + 1 < argc) rate = std::atoi(argv[++i]);
        else if (a == "--pcm" && i + 1 < argc) pcmOut = argv[++i];
        else if (a == "--decoded" && i + 1 < argc) decodedOut = argv[++i];
        else if (a == "--loop") loop = true;
        else if (a == "--trim") trim = true;
        else if (in.empty()) in = a;
        else return usage();
    }
    if (in.empty() || out.empty() || rate <= 0) return usage();
    try {
        splash::MonoAudio src = splash::loadWavMono(in);
        if (trim) splash::trimLeadingSilence(src);
        if (!pcmOut.empty()) writeWav16(pcmOut, splash::toPcm16(src.samples), src.rate);
        splash::MonoAudio res = splash::resample(src, rate);
        std::vector<int16_t> pcm = splash::toPcm16(res.samples);
        std::vector<uint8_t> adpcm = splash::encodeSpuAdpcm(pcm, loop);
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(adpcm.data()),
                                                   std::streamsize(adpcm.size()));
        // SNR of the decoded ADPCM against the PCM it was encoded from
        // (skipping the leading silent block).
        std::vector<int16_t> dec = splash::decodeSpuAdpcm(adpcm);
        double sig = 0, err = 0;
        for (size_t i = 0; i < pcm.size() && i + 28 < dec.size(); i++) {
            double d = double(dec[i + 28]) - double(pcm[i]);
            sig += double(pcm[i]) * pcm[i];
            err += d * d;
        }
        if (!decodedOut.empty()) writeWav16(decodedOut, dec, rate);
        std::printf("%s rate=%d->%d samples=%zu bytes=%zu snr=%.2f\n", in.c_str(), src.rate, rate, pcm.size(),
                    adpcm.size(), err > 0 ? 10 * std::log10(sig / err) : 999.0);
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
    if (argc >= 2 && std::strcmp(argv[1], "audio") == 0) return audio(argc, argv);
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
