#include "import.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>

#include "cgltf.h"
#include "unitymath.hh"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace splash {

namespace {

constexpr int kMaxTextureSide = 256;  // PS1 UVs are 8 bits

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error(msg); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A file name stem safe on every platform the editor runs on.
std::string safeStem(const fs::path& p) {
    std::u8string u = p.stem().u8string();
    std::string s;
    for (char8_t c : u) s += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? char(c) : '_';
    return s.empty() ? std::string("model") : s;
}

std::string projectPath(const fs::path& rel) {
    std::u8string u = rel.generic_u8string();
    return std::string(u.begin(), u.end());
}

const char* resultText(cgltf_result r) {
    switch (r) {
        case cgltf_result_data_too_short: return "the file is cut short";
        case cgltf_result_unknown_format: return "not a glTF file";
        case cgltf_result_invalid_json: return "the JSON is not valid";
        case cgltf_result_invalid_gltf: return "the glTF is not valid";
        case cgltf_result_file_not_found: return "a file it refers to is missing";
        case cgltf_result_io_error: return "it could not be read";
        case cgltf_result_out_of_memory: return "out of memory";
        default: return "it could not be read";
    }
}

struct Pixels {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};

// The encoded bytes of a glTF image: a buffer view (.glb), a data: URI or a
// file next to the model.
std::vector<unsigned char> imageBytes(const cgltf_options& opt, const cgltf_image& img, const fs::path& dir) {
    if (img.buffer_view) {
        const auto* p = static_cast<const unsigned char*>(cgltf_buffer_view_data(img.buffer_view));
        if (!p) fail("an embedded image has no data");
        return {p, p + img.buffer_view->size};
    }
    if (!img.uri) fail("an image has neither a URI nor embedded data");
    std::string uri = img.uri;
    if (uri.rfind("data:", 0) == 0) {
        size_t comma = uri.find(";base64,");
        if (comma == std::string::npos) fail("an image data URI is not base64");
        std::string b64 = uri.substr(comma + 8);
        size_t pad = b64.size() >= 2 && b64[b64.size() - 1] == '=' ? (b64[b64.size() - 2] == '=' ? 2 : 1) : 0;
        size_t size = b64.size() / 4 * 3 - pad;
        void* out = nullptr;
        if (cgltf_load_buffer_base64(&opt, size, b64.c_str(), &out) != cgltf_result_success) fail("an image data URI does not decode");
        std::vector<unsigned char> v(static_cast<unsigned char*>(out), static_cast<unsigned char*>(out) + size);
        std::free(out);
        return v;
    }
    std::vector<char> name(uri.begin(), uri.end());
    name.push_back(0);
    cgltf_decode_uri(name.data());
    fs::path file = dir / fs::path(reinterpret_cast<const char8_t*>(name.data()));
    std::ifstream in(file, std::ios::binary);
    if (!in) fail("missing texture " + projectPath(file.filename()));
    return {std::istreambuf_iterator<char>(in), {}};
}

Pixels decode(const std::vector<unsigned char>& bytes, const std::string& what) {
    Pixels px;
    int n;
    unsigned char* d = stbi_load_from_memory(bytes.data(), int(bytes.size()), &px.w, &px.h, &n, 4);
    if (!d) fail(what + " is not a PNG or JPEG image (" + stbi_failure_reason() + ")");
    px.rgba.assign(d, d + size_t(px.w) * px.h * 4);
    stbi_image_free(d);
    return px;
}

// Scales the image down so neither side exceeds kMaxTextureSide.
bool fit(Pixels& px) {
    if (px.w <= kMaxTextureSide && px.h <= kMaxTextureSide) return false;
    float s = float(kMaxTextureSide) / float(std::max(px.w, px.h));
    int w = std::max(1, int(std::lround(px.w * s))), h = std::max(1, int(std::lround(px.h * s)));
    std::vector<unsigned char> out(size_t(w) * h * 4);
    stbir_resize_uint8_srgb(px.rgba.data(), px.w, px.h, 0, out.data(), w, h, 0, STBIR_RGBA);
    px.w = w;
    px.h = h;
    px.rgba = std::move(out);
    return true;
}

struct Accessor {
    const cgltf_accessor* a = nullptr;
    int comps = 0;
    float at(size_t i, int c) const {
        float v[16] = {};
        cgltf_accessor_read_float(a, i, v, 16);
        return v[c];
    }
};

Accessor attribute(const cgltf_primitive& p, cgltf_attribute_type type, int index) {
    for (size_t i = 0; i < p.attributes_count; i++)
        if (p.attributes[i].type == type && p.attributes[i].index == index)
            return {p.attributes[i].data, int(cgltf_num_components(p.attributes[i].data->type))};
    return {};
}

// Unit length; up for a degenerate vector, so a sliver triangle still lights.
Vec3 unit(Vec3 v) {
    float l = std::sqrt(dot(v, v));
    return l > 0 ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 1, 0};
}

void collectNodes(const cgltf_node* n, std::vector<const cgltf_node*>& out) {
    out.push_back(n);
    for (size_t i = 0; i < n->children_count; i++) collectNodes(n->children[i], out);
}

}  // namespace

bool canImportModel(const fs::path& file) {
    std::string e = lower(projectPath(file.extension()));
    return e == ".gltf" || e == ".glb";
}

ImportedModel importModel(const fs::path& source, const fs::path& project) {
    if (!canImportModel(source)) fail(projectPath(source.filename()) + ": only .glb and .gltf models can be imported");
    const std::string what = projectPath(source.filename());
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    std::u8string src8 = source.u8string();
    std::string src(src8.begin(), src8.end());
    cgltf_result r = cgltf_parse_file(&opt, src.c_str(), &data);
    if (r == cgltf_result_success) r = cgltf_load_buffers(&opt, data, src.c_str());
    if (r == cgltf_result_success) r = cgltf_validate(data);
    if (r != cgltf_result_success) {
        cgltf_free(data);
        fail(what + ": " + resultText(r));
    }
    std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);

    ImportedModel out;
    std::vector<const cgltf_node*> nodes;
    const cgltf_scene* scene = data->scene ? data->scene : data->scenes_count ? &data->scenes[0] : nullptr;
    if (scene)
        for (size_t i = 0; i < scene->nodes_count; i++) collectNodes(scene->nodes[i], nodes);
    else
        for (size_t i = 0; i < data->nodes_count; i++)
            if (!data->nodes[i].parent) collectNodes(&data->nodes[i], nodes);

    Mesh mesh;
    std::map<const cgltf_material*, int> submeshOf;
    std::vector<const cgltf_material*> materialOf;
    bool anyUv = false, anyColor = false, skipped = false, skinned = false;
    for (const cgltf_node* n : nodes)
        for (size_t pi = 0; n->mesh && pi < n->mesh->primitives_count; pi++) {
            const cgltf_primitive& p = n->mesh->primitives[pi];
            anyUv |= attribute(p, cgltf_attribute_type_texcoord, 0).a != nullptr;
            anyColor |= attribute(p, cgltf_attribute_type_color, 0).a != nullptr;
        }

    for (const cgltf_node* n : nodes) {
        if (!n->mesh) continue;
        skinned |= n->skin != nullptr;
        float m[16];
        cgltf_node_transform_world(n, m);
        // Normals take the inverse transpose of the upper 3x3.
        float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], k = m[10];
        float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
        float it[9] = {e * k - f * h, f * g - d * k, d * h - e * g, c * h - b * k, a * k - c * g,
                       b * g - a * h, b * f - c * e, c * d - a * f, a * e - b * d};  // cofactor matrix, rows
        for (size_t pi = 0; pi < n->mesh->primitives_count; pi++) {
            const cgltf_primitive& p = n->mesh->primitives[pi];
            if (p.type != cgltf_primitive_type_triangles) {
                skipped = true;
                continue;
            }
            Accessor pos = attribute(p, cgltf_attribute_type_position, 0);
            if (!pos.a) continue;
            Accessor nrm = attribute(p, cgltf_attribute_type_normal, 0);
            Accessor uv = attribute(p, cgltf_attribute_type_texcoord, 0);
            Accessor col = attribute(p, cgltf_attribute_type_color, 0);

            std::vector<int> idx;
            if (p.indices)
                for (size_t i = 0; i < p.indices->count; i++) idx.push_back(int(cgltf_accessor_read_index(p.indices, i)));
            else
                for (size_t i = 0; i < pos.a->count; i++) idx.push_back(int(i));
            idx.resize(idx.size() / 3 * 3);

            auto it2 = submeshOf.find(p.material);
            if (it2 == submeshOf.end()) {
                it2 = submeshOf.emplace(p.material, int(mesh.submeshes.size())).first;
                mesh.submeshes.emplace_back();
                materialOf.push_back(p.material);
            }
            std::vector<int>& sm = mesh.submeshes[size_t(it2->second)];

            // glTF (right-handed, +Z forward) to the .mesh space (left-handed):
            // mirror X. Mirroring, and a node with a negative determinant,
            // each flip which way a triangle faces, so the winding is reversed
            // once for the mirror and again for each negative scale.
            bool reverse = det >= 0;
            auto vertex = [&](size_t i, Vec3 faceNormal, bool flat) {
                float x = pos.at(i, 0), y = pos.at(i, 1), z = pos.at(i, 2);
                Vec3 w{m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
                       m[2] * x + m[6] * y + m[10] * z + m[14]};
                mesh.positions.push_back({-w.x, w.y, w.z});
                Vec3 nn = faceNormal;
                if (!flat) {
                    float nx = nrm.at(i, 0), ny = nrm.at(i, 1), nz = nrm.at(i, 2);
                    Vec3 t{it[0] * nx + it[1] * ny + it[2] * nz, it[3] * nx + it[4] * ny + it[5] * nz,
                           it[6] * nx + it[7] * ny + it[8] * nz};
                    if (det < 0) t = {-t.x, -t.y, -t.z};
                    t = unit(t);
                    nn = {-t.x, t.y, t.z};
                }
                mesh.normals.push_back(nn);
                if (anyUv) mesh.uv.push_back(uv.a ? Vec2{uv.at(i, 0), 1.0f - uv.at(i, 1)} : Vec2{0, 0});
                if (anyColor)
                    mesh.colors.push_back(col.a ? std::array<float, 4>{col.at(i, 0), col.at(i, 1), col.at(i, 2),
                                                                         col.comps == 4 ? col.at(i, 3) : 1.0f}
                                                : std::array<float, 4>{1, 1, 1, 1});
                return int(mesh.positions.size() - 1);
            };
            if (nrm.a) {
                int base = int(mesh.positions.size());
                for (size_t i = 0; i < pos.a->count; i++) vertex(i, {}, false);
                for (size_t t = 0; t < idx.size(); t += 3) {
                    int i0 = base + idx[t], i1 = base + idx[t + 1], i2 = base + idx[t + 2];
                    if (reverse) std::swap(i1, i2);
                    sm.insert(sm.end(), {i0, i1, i2});
                }
            } else {
                // No normals: flat shading, each triangle gets its own corners.
                for (size_t t = 0; t < idx.size(); t += 3) {
                    int c0 = vertex(size_t(idx[t]), {}, true), c1 = vertex(size_t(idx[t + 1]), {}, true),
                        c2 = vertex(size_t(idx[t + 2]), {}, true);
                    if (reverse) std::swap(c1, c2);
                    const Vec3 &p0 = mesh.positions[size_t(c0)], &p1 = mesh.positions[size_t(c1)], &p2 = mesh.positions[size_t(c2)];
                    Vec3 fn = unit(cross(p1 - p0, p2 - p0));
                    for (int ci : {c0, c1, c2}) mesh.normals[size_t(ci)] = fn;
                    sm.insert(sm.end(), {c0, c1, c2});
                }
            }
        }
    }
    for (auto& sm : mesh.submeshes) out.triangles += int(sm.size() / 3);
    if (out.triangles == 0) fail(what + " has no triangles to import");
    if (skipped) out.warnings.push_back("points and lines were left out, only triangles are imported");
    if (skinned) out.warnings.push_back("the skeleton was left out, the model is imported in its rest pose");

    const std::string stem = safeStem(source);
    const fs::path dir = "models";
    std::error_code ec;
    fs::create_directories(project / dir, ec);
    if (ec) fail("cannot create " + projectPath(project / dir) + ": " + ec.message());

    // Textures: each image used as a base colour, written once.
    std::map<const cgltf_image*, std::string> written;
    int imagesUsed = 0;
    for (const cgltf_material* mat : materialOf) {
        const cgltf_texture* t = nullptr;
        if (mat && mat->has_pbr_metallic_roughness) t = mat->pbr_metallic_roughness.base_color_texture.texture;
        else if (mat && mat->has_pbr_specular_glossiness) t = mat->pbr_specular_glossiness.diffuse_texture.texture;
        if (t && t->image && !written.count(t->image)) written[t->image] = {}, imagesUsed++;
    }
    int n = 0;
    for (const cgltf_material* mat : materialOf) {
        Material outMat;
        const cgltf_texture* t = nullptr;
        if (mat && mat->has_pbr_metallic_roughness) {
            const float* c = mat->pbr_metallic_roughness.base_color_factor;
            outMat.color = {c[0], c[1], c[2], c[3]};
            t = mat->pbr_metallic_roughness.base_color_texture.texture;
        } else if (mat && mat->has_pbr_specular_glossiness) {
            const float* c = mat->pbr_specular_glossiness.diffuse_factor;
            outMat.color = {c[0], c[1], c[2], c[3]};
            t = mat->pbr_specular_glossiness.diffuse_texture.texture;
        }
        if (t && t->image) {
            std::string& rel = written[t->image];
            if (rel.empty()) {
                Pixels px = decode(imageBytes(opt, *t->image, source.parent_path()), what + " texture");
                if (fit(px))
                    out.warnings.push_back("a texture was scaled down to " + std::to_string(px.w) + " x " + std::to_string(px.h) +
                                           ", the most a PS1 texture can be");
                fs::path file = dir / (imagesUsed == 1 ? stem + ".png" : stem + "_" + std::to_string(n++) + ".png");
                if (!stbi_write_png(projectPath(project / file).c_str(), px.w, px.h, 4, px.rgba.data(), px.w * 4))
                    fail("cannot write " + projectPath(file));
                rel = projectPath(file);
                out.textures.push_back(rel);
            }
            outMat.texture = rel;
        }
        out.materials.push_back(outMat);
    }

    fs::path meshFile = dir / (stem + ".mesh");
    saveMesh(mesh, project / meshFile);
    out.mesh = projectPath(meshFile);
    return out;
}

}  // namespace splash
