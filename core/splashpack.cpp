#include "splashpack.hh"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "audio.hh"
#include "binwriter.hh"
#include "bvh.hh"
#include "luacompile.hh"
#include "navregion.hh"
#include "texture.hh"
#include "vrampacker.hh"

namespace splash {

namespace fs = std::filesystem;

namespace {

struct PsxVert {
    int16_t vx, vy, vz, nx, ny, nz;
    uint8_t u, v, r, g, b;
};

struct PsxTri {
    PsxVert v[3];
    int textureIndex;  // -1 = untextured
};

struct Color {
    float r = 0, g = 0, b = 0;
};

struct SceneLight {
    LightKind kind;
    Vec3 position, forward;
    float r, g, b, intensity, spotAngle, innerSpotAngle;
};

struct ExpObject {
    const FlatObject* flat;
    const Object* obj;
    const MeshComponent* mc;
    const Mesh* mesh;
    std::vector<PsxTexture*> textures;  // per-material list as built, before packing
    std::vector<PsxTri> tris;
    std::vector<PsxTexture*> finalTextures;  // deduplicated, after packing
};

// String.Substring(0, 24) counts UTF-16 code units; names are stored as UTF-8.
std::string truncateUtf16(const std::string& s, size_t units) {
    size_t i = 0, n = 0;
    while (i < s.size()) {
        unsigned char c = uint8_t(s[i]);
        size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        size_t u = len == 4 ? 2 : 1;
        if (n + u > units) break;
        n += u;
        i += len;
    }
    return s.substr(0, i);
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// PSXLightingBaker.ComputeLighting
Color bake(Vec3 vertex, Vec3 normal, const std::vector<SceneLight>& lights) {
    Color fin;
    for (const SceneLight& l : lights) {
        Color c;
        auto lit = [&](float k) {
            // light.color * light.intensity * ... evaluated left to right
            c = {l.r * l.intensity, l.g * l.intensity, l.b * l.intensity};
            return k;
        };
        if (l.kind == LightKind::Directional) {
            Vec3 dir = l.forward * -1.f;
            float ndl = mfMax(0.f, dot(normal, dir));
            lit(0);
            c = {c.r * ndl, c.g * ndl, c.b * ndl};
        } else if (l.kind == LightKind::Point) {
            Vec3 dir = l.position - vertex;
            float dist = magnitude(dir);
            dir = normalized(dir);
            float ndl = mfMax(0.f, dot(normal, dir));
            float att = 1.0f / mfMax(dist * dist, 0.0001f);
            lit(0);
            c = {c.r * ndl, c.g * ndl, c.b * ndl};
            c = {c.r * att, c.g * att, c.b * att};
        } else {
            Vec3 L = l.position - vertex;
            float dist = magnitude(L);
            L = L / dist;
            float ndl = mfMax(0.f, dot(normal, L));
            float att = 1.0f / mfMax(dist * dist, 0.0001f);
            constexpr float deg2rad = 0.0174532924f;  // Mathf.Deg2Rad
            float outer = (l.spotAngle * 0.5f) * deg2rad;
            float inner = outer * 0.8f;
            if (l.innerSpotAngle > 0) inner = (l.innerSpotAngle * 0.5f) * deg2rad;
            float cosOuter = float(std::cos(double(outer)));
            float cosInner = float(std::cos(double(inner)));
            float cosAngle = dot(L, l.forward * -1.f);
            if (cosAngle >= cosOuter) {
                float sf = clampv((cosAngle - cosOuter) / (cosInner - cosOuter), 0.f, 1.f);
                sf = float(std::pow(double(sf), 4.0));
                lit(0);
                c = {c.r * ndl, c.g * ndl, c.b * ndl};
                c = {c.r * att, c.g * att, c.b * att};
                c = {c.r * sf, c.g * sf, c.b * sf};
            }
        }
        fin.r += c.r;
        fin.g += c.g;
        fin.b += c.b;
    }
    fin.r = clampv(fin.r, 0.f, 0.8f);
    fin.g = clampv(fin.g, 0.f, 0.8f);
    fin.b = clampv(fin.b, 0.f, 0.8f);
    return fin;
}

// PSXMesh.RecalculateSmoothNormals: average over vertices with identical
// positions (exact float equality, as Dictionary<Vector3> keys).
std::vector<Vec3> smoothNormals(const Mesh& m) {
    struct K {
        float x, y, z;
        bool operator<(const K& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };
    std::map<K, std::vector<size_t>> groups;
    for (size_t i = 0; i < m.positions.size(); i++)
        groups[{m.positions[i].x, m.positions[i].y, m.positions[i].z}].push_back(i);
    std::vector<Vec3> out(m.positions.size());
    for (auto& [k, idx] : groups) {
        Vec3 s{};
        for (size_t i : idx) s = s + m.normals[i];
        s = normalized(s);
        for (size_t i : idx) out[i] = s;
    }
    return out;
}

PsxVert toPsxVertex(Vec3 vertex, float gte, Vec3 normal, Vec2 uv, int width, int height, Color c) {
    PsxVert p;
    p.vx = toPsxCoord(vertex.x, gte);
    p.vy = toPsxCoord(-vertex.y, gte);
    p.vz = toPsxCoord(vertex.z, gte);
    p.nx = toPsxCoord(normal.x);
    p.ny = toPsxCoord(-normal.y);
    p.nz = toPsxCoord(normal.z);
    p.u = uint8_t(clampv(uv.x * float(width - 1), 0.f, 255.f));
    p.v = uint8_t(clampv((1.0f - uv.y) * float(height - 1), 0.f, 255.f));
    p.r = colorToPsx(c.r);
    p.g = colorToPsx(c.g);
    p.b = colorToPsx(c.b);
    return p;
}

// PSXMesh.BuildFromMesh
void buildTris(ExpObject& e, float gte, const std::vector<SceneLight>& lights, ExportResult& res) {
    const Mesh& m = *e.mesh;
    const MeshComponent& mc = *e.mc;
    const FlatObject& fo = *e.flat;
    if (m.normals.empty()) {
        res.errors.push_back(e.obj->name + ": mesh " + mc.mesh + " has no normals");
        return;
    }
    if (mc.materials.empty()) {
        res.errors.push_back(e.obj->name + ": mesh component has no materials");
        return;
    }
    std::vector<Vec3> lightNormals = mc.smoothNormals ? smoothNormals(m) : m.normals;
    bool hasColors = mc.vertexColors == VertexColorMode::Mesh && m.colors.size() == m.positions.size();
    std::vector<Vec3> wv(m.positions.size()), wn(m.positions.size());
    for (size_t i = 0; i < m.positions.size(); i++) {
        wv[i] = fo.localToWorld.point(m.positions[i]);
        wn[i] = normalized(rotate(fo.worldRotation, lightNormals[i]));
    }
    std::vector<Vec2> uvs = m.uv;
    if (uvs.empty()) uvs.assign(m.positions.size(), {});

    for (size_t sub = 0; sub < m.submeshes.size(); sub++) {
        size_t mi = std::min(sub, mc.materials.size() - 1);
        const Material& mat = mc.materials[mi];
        int texIndex = -1;
        if (!mat.texture.empty())
            for (size_t i = 0; i < e.textures.size(); i++)
                if (e.textures[i]->source == mat.texture) {
                    texIndex = int(i);
                    break;
                }
        auto convert = [&](int idx) {
            size_t i = size_t(idx);
            Vec3 v = scale(m.positions[i], fo.lossyScale);
            Color c;
            switch (mc.vertexColors) {
                case VertexColorMode::Flat:
                    c = {mc.flatColor[0] / 255.f, mc.flatColor[1] / 255.f, mc.flatColor[2] / 255.f};
                    break;
                case VertexColorMode::Mesh:
                    if (hasColors)
                        c = {m.colors[i][0], m.colors[i][1], m.colors[i][2]};
                    else
                        c = {0.5f, 0.5f, 0.5f};
                    break;
                default:
                    c = bake(wv[i], wn[i], lights);
                    break;
            }
            if (texIndex == -1) {
                c = {c.r * mat.color[0], c.g * mat.color[1], c.b * mat.color[2]};
                return toPsxVertex(v, gte, m.normals[i], {}, 0, 0, c);
            }
            const PsxTexture* t = e.textures[size_t(texIndex)];
            return toPsxVertex(v, gte, m.normals[i], uvs[i], t->width, t->height, c);
        };
        const auto& tri = m.submeshes[sub];
        for (size_t i = 0; i + 2 < tri.size(); i += 3) {
            int a = tri[i], b = tri[i + 1], c = tri[i + 2];
            Vec3 p0 = m.positions[size_t(a)];
            Vec3 fn = normalized(cross(m.positions[size_t(b)] - p0, m.positions[size_t(c)] - p0));
            if (dot(fn, m.normals[size_t(a)]) < 0) std::swap(b, c);
            e.tris.push_back({{convert(a), convert(b), convert(c)}, texIndex});
        }
    }
}

void writeWorldAabb(BinWriter& w, const Mat34& m, const Bounds& local, float gte) {
    Vec3 ext = local.extents, center = local.center;
    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
    for (int i = 0; i < 8; i++) {
        Vec3 corner = center + Vec3{(i & 1) ? ext.x : -ext.x, (i & 2) ? ext.y : -ext.y, (i & 4) ? ext.z : -ext.z};
        Vec3 world = m.point(corner);
        mn = vmin(mn, world);
        mx = vmax(mx, world);
    }
    w.i32(toWorldFixed12(mn.x / gte));
    w.i32(toWorldFixed12(-mx.y / gte));
    w.i32(toWorldFixed12(mn.z / gte));
    w.i32(toWorldFixed12(mx.x / gte));
    w.i32(toWorldFixed12(-mn.y / gte));
    w.i32(toWorldFixed12(mx.z / gte));
}

// PSXTrig.ConvertRotationToPSXMatrix
void writeRotation(BinWriter& w, Quat q) {
    float x = q.x, y = q.y, z = q.z, qw = q.w;
    float m00 = 1.f - 2.f * (y * y + z * z);
    float m01 = 2.f * (x * y - z * qw);
    float m02 = 2.f * (x * z + y * qw);
    float m10 = 2.f * (x * y + z * qw);
    float m11 = 1.f - 2.f * (x * x + z * z);
    float m12 = 2.f * (y * z - x * qw);
    float m20 = 2.f * (x * z - y * qw);
    float m21 = 2.f * (y * z + x * qw);
    float m22 = 1.f - 2.f * (x * x + y * y);
    float r[3][3] = {{m00, -m01, m02}, {-m10, m11, -m12}, {m20, -m21, m22}};
    for (auto& row : r)
        for (float v : row) w.i32(toFixed12(v));
}

// PSXPlayer.FindNavmesh: Physics.Raycast(transform.position, Vector3.down,
// 100) and the hit point raised by the player height. The scene format has no
// physics colliders, so the ray is cast against the front faces of every
// active mesh (what Unity's primitive and mesh colliders amount to).
Vec3 findCamPoint(Vec3 origin, float height, const std::vector<ExpObject>& exporters) {
    double bestT = 100.0;
    bool hit = false;
    for (const ExpObject& e : exporters) {
        if (!e.obj->active || !e.flat->activeInHierarchy) continue;
        const Mat34& m = e.flat->localToWorld;
        for (const auto& sm : e.mesh->submeshes)
            for (size_t i = 0; i + 2 < sm.size(); i += 3) {
                Vec3 a = m.point(e.mesh->positions[size_t(sm[i])]);
                Vec3 b = m.point(e.mesh->positions[size_t(sm[i + 1])]);
                Vec3 c = m.point(e.mesh->positions[size_t(sm[i + 2])]);
                // Edge functions in XZ; a front face seen from above has normal.y > 0.
                double ax = a.x - double(origin.x), az = a.z - double(origin.z);
                double bx = b.x - double(origin.x), bz = b.z - double(origin.z);
                double cx = c.x - double(origin.x), cz = c.z - double(origin.z);
                double w0 = bz * cx - bx * cz, w1 = cz * ax - cx * az, w2 = az * bx - ax * bz;
                double area = w0 + w1 + w2;
                if (area <= 0 || w0 < 0 || w1 < 0 || w2 < 0) continue;
                double y = (w0 * a.y + w1 * b.y + w2 * c.y) / area;
                double t = double(origin.y) - y;
                if (t < 0 || t > bestT) continue;
                bestT = t;
                hit = true;
            }
    }
    if (!hit) return origin + Vec3{0, height, 0};
    Vec3 point{origin.x, float(double(origin.y) - bestT), origin.z};
    return point + Vec3{0, height, 0};
}

uint32_t hashSceneId(const std::string& id) {
    if (id.empty()) return 0;
    uint32_t h = 2166136261u;
    // FNV-1a over the low byte of each UTF-16 unit; ASCII ids only for now.
    for (unsigned char c : id) {
        h ^= c;
        h *= 16777619u;
    }
    return h == 0 ? 1 : h;
}

}  // namespace

ExportResult exportSplashpack(const Scene& scene, const fs::path& root, const fs::path& out,
                              const ExportOptions& options) {
    ExportResult res;
    const float gte = scene.settings.gteScaling;
    std::vector<FlatObject> flat = flatten(scene);

    std::map<std::string, std::unique_ptr<Mesh>> meshes;
    std::map<std::string, Image> images;
    std::deque<PsxTexture> textureStore;

    auto meshFor = [&](const std::string& p) -> const Mesh* {
        auto it = meshes.find(p);
        if (it == meshes.end()) it = meshes.emplace(p, std::make_unique<Mesh>(loadMesh(root / p))).first;
        return it->second.get();
    };
    auto imageFor = [&](const std::string& p) -> const Image& {
        auto it = images.find(p);
        if (it == images.end()) it = images.emplace(p, loadImage(root / p)).first;
        return it->second;
    };

    std::vector<SceneLight> lights;
    for (const FlatObject& fo : flat) {
        if (!fo.object->light || !fo.object->light->enabled) continue;
        const LightComponent& l = *fo.object->light;
        lights.push_back({l.kind, fo.worldPosition, rotate(fo.worldRotation, {0, 0, 1}), l.color[0], l.color[1],
                          l.color[2], l.intensity, l.spotAngle, l.innerSpotAngle});
    }

    std::vector<ExpObject> exporters;
    for (const FlatObject& fo : flat) {
        if (!fo.object->mesh) continue;
        ExpObject e{&fo, fo.object, &*fo.object->mesh, nullptr, {}, {}, {}};
        try {
            e.mesh = meshFor(e.mc->mesh);
        } catch (const std::exception& ex) {
            res.errors.push_back(ex.what());
            continue;
        }
        // PSXObjectExporter.CreatePSXTextures2D: one texture per textured
        // material, reusing the object's own copy for a repeated source.
        std::map<std::string, PsxTexture*> cache;
        for (const Material& mat : e.mc->materials) {
            if (mat.texture.empty()) continue;
            auto it = cache.find(mat.texture);
            if (it != cache.end()) {
                e.textures.push_back(it->second);
                continue;
            }
            try {
                textureStore.push_back(convertTexture(imageFor(mat.texture), e.mc->bitDepth));
            } catch (const std::exception& ex) {
                res.errors.push_back(ex.what());
                continue;
            }
            textureStore.back().source = mat.texture;
            cache[mat.texture] = &textureStore.back();
            e.textures.push_back(&textureStore.back());
        }
        exporters.push_back(std::move(e));
    }
    if (!res.ok()) return res;

    if (!options.objectOrder.empty()) {
        auto rank = [&](const ExpObject& e) {
            auto it = std::find(options.objectOrder.begin(), options.objectOrder.end(), e.obj->name);
            return it - options.objectOrder.begin();
        };
        std::stable_sort(exporters.begin(), exporters.end(),
                         [&](const ExpObject& a, const ExpObject& b) { return rank(a) < rank(b); });
    }

    for (ExpObject& e : exporters) buildTris(e, gte, lights, res);
    if (!res.ok()) return res;

    // VRAM packing over every object's texture list in object order.
    std::vector<PsxTexture*> all;
    for (ExpObject& e : exporters) all.insert(all.end(), e.textures.begin(), e.textures.end());
    VramLayout vram = packVram(all, options.vram, res.errors);
    if (!res.ok()) return res;

    // Remap each object's texture list to the packed (deduplicated) textures.
    std::map<std::string, PsxTexture*> packedBySource;
    for (Atlas& a : vram.atlases)
        for (PsxTexture* t : a.textures) packedBySource[t->source + "#" + std::to_string(int(t->bitDepth))] = t;
    for (ExpObject& e : exporters) {
        std::vector<int> remap(e.textures.size(), -1);
        for (size_t i = 0; i < e.textures.size(); i++) {
            auto it = packedBySource.find(e.textures[i]->source + "#" + std::to_string(int(e.textures[i]->bitDepth)));
            if (it == packedBySource.end()) continue;
            auto f = std::find(e.finalTextures.begin(), e.finalTextures.end(), it->second);
            if (f == e.finalTextures.end()) {
                e.finalTextures.push_back(it->second);
                f = e.finalTextures.end() - 1;
            }
            remap[i] = int(f - e.finalTextures.begin());
        }
        for (PsxTri& t : e.tris)
            if (t.textureIndex >= 0) t.textureIndex = remap[size_t(t.textureIndex)];
    }

    // BVH over active objects' meshes (mesh.triangles = submeshes concatenated).
    std::vector<BvhInputObject> bvhIn;
    for (ExpObject& e : exporters) {
        BvhInputObject b{e.obj->active, e.flat->localToWorld, &e.mesh->positions, {}};
        for (const auto& sm : e.mesh->submeshes) b.triangles.insert(b.triangles.end(), sm.begin(), sm.end());
        bvhIn.push_back(std::move(b));
    }
    Bvh bvh = buildBvh(bvhIn);

    // Lua files: object scripts in object order, then the scene script.
    std::vector<std::string> luaFiles;
    auto addLua = [&](const std::string& p) {
        if (!p.empty() && std::find(luaFiles.begin(), luaFiles.end(), p) == luaFiles.end()) luaFiles.push_back(p);
    };
    // Trigger boxes and interactables: active objects only (FindObjectsByType
    // skips inactive ones), in canonical order.
    std::vector<const FlatObject*> triggers, interactables, audioSources;
    for (const FlatObject& fo : flat) {
        if (!fo.activeInHierarchy || !fo.object->active) continue;
        if (fo.object->trigger) triggers.push_back(&fo);
        if (fo.object->interactable) interactables.push_back(&fo);
        if (fo.object->audio) audioSources.push_back(&fo);
    }
    // Audio clips: SPU-ADPCM as `psxavenc -t spu -f <rate> [-L]` writes it.
    std::vector<std::vector<uint8_t>> audioData;
    for (const FlatObject* fo : audioSources) {
        const AudioComponent& a = *fo->object->audio;
        std::vector<uint8_t> data;
        if (!a.clip.empty()) {
            try {
                MonoAudio src = loadWavMono(root / a.clip);
                if (a.trimLeadingSilence) trimLeadingSilence(src);
                data = encodeSpuAdpcm(toPcm16(resample(src, a.sampleRate).samples), a.loop);
            } catch (const std::exception& ex) {
                res.errors.push_back(ex.what());
            }
        }
        audioData.push_back(std::move(data));
    }
    for (ExpObject& e : exporters)
        if (e.obj->script) addLua(e.obj->script->lua);
    addLua(scene.settings.script);
    for (const FlatObject* t : triggers) addLua(t->object->trigger->lua);
    std::vector<std::string> luaData;
    for (const std::string& p : luaFiles) {
        try {
            std::string src = readFile(root / p);
            if (options.luaBytecode) {
                std::vector<uint8_t> bc = compileLua(src, p);
                src.assign(bc.begin(), bc.end());
            }
            luaData.push_back(std::move(src));
        } catch (const std::exception& ex) {
            res.errors.push_back(ex.what());
        }
    }
    if (!res.ok()) return res;
    auto luaIndex = [&](const std::string& p) -> int16_t {
        auto it = std::find(luaFiles.begin(), luaFiles.end(), p);
        return it == luaFiles.end() ? int16_t(-1) : int16_t(it - luaFiles.begin());
    };

    int clutCount = 0;
    for (Atlas& a : vram.atlases)
        for (PsxTexture* t : a.textures)
            if (t->hasPalette) clutCount++;
    int colliderCount = 0;
    for (ExpObject& e : exporters)
        if (e.obj->collider && e.obj->collider->kind == ColliderKind::Dynamic) colliderCount++;

    // PSXSceneExporter: the first active PSXPlayer and PSXNavigationSettings
    // (FindObjectsByType skips inactive objects), in canonical order.
    const FlatObject* playerObj = nullptr;
    const FlatObject* navObj = nullptr;
    for (const FlatObject& fo : flat) {
        if (!fo.activeInHierarchy || !fo.object->active) continue;
        if (fo.object->player && !playerObj) playerObj = &fo;
        if (fo.object->navigation && !navObj) navObj = &fo;
    }
    Vec3 navSpawn{};
    if (navObj) {
        navSpawn = navObj->worldPosition;
        const std::string& anchor = navObj->object->navigation->spawnAnchor;
        if (!anchor.empty()) {
            auto it = std::find_if(flat.begin(), flat.end(),
                                   [&](const FlatObject& fo) { return fo.object->name == anchor; });
            if (it == flat.end()) {
                res.errors.push_back(navObj->object->name + ": spawnAnchor '" + anchor + "' not found");
                return res;
            }
            navSpawn = it->worldPosition;
        }
    }
    Vec3 playerPos{};
    Quat playerRot{};
    float playerHeight = 1.8f, playerRadius = 0.5f, moveSpeed = 3.f, sprintSpeed = 8.f, jumpHeight = 2.f,
          gravity = 20.f;
    if (playerObj) {
        const PlayerComponent& pc = *playerObj->object->player;
        playerPos = findCamPoint(playerObj->worldPosition, pc.playerHeight, exporters);
        playerRot = playerObj->worldRotation;
        playerHeight = pc.playerHeight;
        playerRadius = pc.playerRadius;
        moveSpeed = pc.moveSpeed;
        sprintSpeed = pc.sprintSpeed;
        jumpHeight = pc.jumpHeight;
        gravity = pc.gravity;
    } else if (navObj) {
        playerPos = navSpawn;
        playerHeight = navObj->object->navigation->agentHeight;
        playerRadius = navObj->object->navigation->agentRadius;
    }

    // Nav regions. Agent size comes from the player (or its defaults); the
    // navigation settings override everything when present.
    NavBuildParams np;
    np.agentRadius = playerRadius;
    np.agentHeight = playerHeight;
    const NavBakeSettings* bake = nullptr;
    if (navObj) {
        np.agentRadius = navObj->object->navigation->agentRadius;
        np.agentHeight = navObj->object->navigation->agentHeight;
        bake = &navObj->object->navigation->nav;
    } else if (playerObj) {
        bake = &playerObj->object->player->nav;
    }
    if (bake) {
        np.maxStepHeight = bake->maxStepHeight;
        np.walkableSlopeAngle = bake->walkableSlopeAngle;
        np.cellSize = bake->cellSize;
        np.cellHeight = bake->cellHeight;
        np.minRegionArea = bake->minRegionArea;
        np.mergeRegionArea = bake->mergeRegionArea;
        np.maxSimplifyError = bake->maxSimplifyError;
        np.maxEdgeLength = bake->maxEdgeLength;
        np.partition = bake->partition;
        np.detailSampleDist = bake->detailSampleDist;
        np.detailMaxError = bake->detailMaxError;
        np.maxPlaneError = bake->maxPlaneError;
    }
    std::vector<NavInputObject> navIn;
    for (ExpObject& e : exporters) {
        NavInputObject n{e.obj->collider && e.obj->collider->kind == ColliderKind::Static,
                         e.obj->collider && e.obj->collider->platform,
                         e.flat->localToWorld,
                         &e.mesh->positions,
                         {},
                         e.mesh->bounds()};
        for (const auto& sm : e.mesh->submeshes) n.triangles.insert(n.triangles.end(), sm.begin(), sm.end());
        navIn.push_back(std::move(n));
    }
    NavMesh nav = buildNavRegions(navIn, np, playerObj ? playerPos : navObj ? navSpawn : playerPos, res.errors);
    if (!res.ok()) return res;
    if (nav.regions.size() > 65535 || nav.portals.size() > 65535) {
        res.errors.push_back("too many nav regions or portals");
        return res;
    }

    BinWriter w;
    // ---- header (144 bytes, v23)
    w.u8('S');
    w.u8('P');
    w.u16(23);
    w.u16(uint16_t(luaFiles.size()));
    w.u16(uint16_t(exporters.size()));
    w.u16(uint16_t(vram.atlases.size()));
    w.u16(uint16_t(clutCount));
    w.u16(uint16_t(colliderCount));
    w.u16(uint16_t(interactables.size()));
    w.i16(toPsxCoord(playerPos.x, gte));
    w.i16(toPsxCoord(-playerPos.y, gte));
    w.i16(toPsxCoord(playerPos.z, gte));
    // Euler angles in 4.12 units of pi: the engine casts these to psyqo::Angle.
    // (2.4.0 writes radians, so any start yaw other than 0 faces the wrong way.)
    Vec3 euler = eulerAngles(playerRot);
    w.i16(toFixed12(euler.x / 180.f));
    w.i16(toFixed12(euler.y / 180.f));
    w.i16(toFixed12(euler.z / 180.f));
    w.u16(uint16_t(toPsxCoord(playerHeight, gte)));
    w.i16(scene.settings.script.empty() ? int16_t(-1) : luaIndex(scene.settings.script));
    w.u16(uint16_t(std::min<size_t>(bvh.nodes.size(), 65535)));
    w.u16(uint16_t(std::min<size_t>(bvh.refs.size(), 65535)));
    w.u16(uint16_t(scene.settings.sceneType));
    w.u16(uint16_t(triggers.size()));
    w.u16(0);  // world collision mesh count (removed)
    w.u16(0);  // world collision tri count (removed)
    w.u16(uint16_t(nav.regions.size()));
    w.u16(uint16_t(nav.portals.size()));
    {
        const float fps = 30.f;
        float movePerFrame = moveSpeed / fps / gte;
        float sprintPerFrame = sprintSpeed / fps / gte;
        w.u16(uint16_t(clampv(roundToInt(movePerFrame * 4096.f), 0, 65535)));
        w.u16(uint16_t(clampv(roundToInt(sprintPerFrame * 4096.f), 0, 65535)));
        float jumpVel = float(std::sqrt(double(2.f * gravity * jumpHeight))) / gte;
        w.u16(uint16_t(clampv(roundToInt(jumpVel * 4096.f), 0, 65535)));
        float grav = gravity / gte;
        w.u16(uint16_t(clampv(roundToInt(grav * 4096.f), 0, 65535)));
        w.u16(uint16_t(toPsxCoord(playerRadius, gte)));
        w.u16(0);
    }
    size_t nameTableOffsetPos = w.pos();
    w.u32(0);
    w.u16(uint16_t(audioSources.size()));
    w.u16(0);
    size_t audioTableOffsetPos = w.pos();
    w.u32(0);
    const FogSettings& fog = scene.settings.fog;
    w.u8(fog.enabled ? 1 : 0);
    for (float c : fog.color) w.u8(uint8_t(clampv(roundToInt(c * 255.f), 0, 255)));
    w.u8(uint8_t(clampv(fog.density, 1, 10)));
    w.u8(0);
    w.u16(0);  // rooms
    w.u16(0);  // portals
    w.u16(0);  // room tri refs
    w.u16(0);  // cutscenes
    w.u16(0);  // room cells
    w.u32(0);  // cutscene table offset
    w.u16(0);  // ui canvases
    w.u8(0);   // ui fonts
    w.u8(0);
    w.u32(0);  // ui table offset
    w.u32(0);  // pixel data offset
    w.u16(0);  // animations
    w.u16(0);  // room portal refs
    w.u32(0);  // animation table offset
    w.u16(0);  // skinned meshes
    w.u16(0);  // agents
    w.u32(0);  // skin table offset
    w.u32(0);  // memcard table offset
    w.u32(0);  // stream table offset
    w.u32(0);  // sprite table offset
    w.u16(0);  // sprite sheets
    w.u16(0);  // sprite anims
    w.u32(hashSceneId(scene.settings.networkId));
    w.u32(0);  // tilemap table offset

    // ---- Lua metadata
    std::vector<size_t> luaOffsetPos;
    for (const std::string& d : luaData) {
        luaOffsetPos.push_back(w.pos());
        w.u32(0);
        w.u32(uint32_t(d.size()));
    }

    // ---- game objects (92 bytes)
    std::vector<size_t> meshOffsetPos;
    for (ExpObject& e : exporters) {
        meshOffsetPos.push_back(w.pos());
        w.u32(0);
        Vec3 pos = e.flat->localToWorld.position();
        w.i32(toWorldFixed12(pos.x / gte));
        w.i32(toWorldFixed12(-pos.y / gte));
        w.i32(toWorldFixed12(pos.z / gte));
        writeRotation(w, e.flat->worldRotation);
        w.u16(uint16_t(e.tris.size()));
        w.i16(e.obj->script ? luaIndex(e.obj->script->lua) : int16_t(-1));
        w.u32(e.obj->active ? 1 : 0);
        {
            auto it = std::find_if(interactables.begin(), interactables.end(),
                                   [&](const FlatObject* fo) { return fo->object == e.obj; });
            w.u16(it == interactables.end() ? 0xFFFF : uint16_t(it - interactables.begin()));
        }
        w.u16(0);       // uv offset (legacy)
        w.u32(0);       // event mask
        writeWorldAabb(w, e.flat->localToWorld, e.mesh->bounds(), gte);
    }

    // ---- colliders (32 bytes, dynamic only)
    for (size_t i = 0; i < exporters.size(); i++) {
        ExpObject& e = exporters[i];
        if (!e.obj->collider || e.obj->collider->kind != ColliderKind::Dynamic) continue;
        writeWorldAabb(w, e.flat->localToWorld, e.mesh->bounds(), gte);
        w.u8(1);
        w.u8(0xFF);
        w.u16(uint16_t(i));
        w.u32(0);
    }

    // ---- trigger boxes (32 bytes): world AABB of the transformed box
    for (const FlatObject* fo : triggers) {
        const TriggerComponent& t = *fo->object->trigger;
        Vec3 half = t.size * 0.5f;
        Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX}, mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (int i = 0; i < 8; i++) {
            Vec3 c{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z};
            Vec3 p = fo->localToWorld.point(c);
            mn = vmin(mn, p);
            mx = vmax(mx, p);
        }
        w.i32(toWorldFixed12(mn.x / gte));
        w.i32(toWorldFixed12(-mx.y / gte));
        w.i32(toWorldFixed12(mn.z / gte));
        w.i32(toWorldFixed12(mx.x / gte));
        w.i32(toWorldFixed12(-mn.y / gte));
        w.i32(toWorldFixed12(mx.z / gte));
        w.i16(t.lua.empty() ? int16_t(-1) : luaIndex(t.lua));
        w.u16(0);
        w.u32(0);
    }

    // ---- BVH
    w.align4();
    for (const BvhNode& n : bvh.nodes) {
        Vec3 mn = n.bounds.min(), mx = n.bounds.max();
        w.i32(toWorldFixed12(mn.x / gte));
        w.i32(toWorldFixed12(-mx.y / gte));
        w.i32(toWorldFixed12(mn.z / gte));
        w.i32(toWorldFixed12(mx.x / gte));
        w.i32(toWorldFixed12(-mn.y / gte));
        w.i32(toWorldFixed12(mx.z / gte));
        w.u16(n.left >= 0 ? uint16_t(n.left) : 0xFFFF);
        w.u16(n.right >= 0 ? uint16_t(n.right) : 0xFFFF);
        w.u16(n.firstTriangle >= 0 ? uint16_t(n.firstTriangle) : 0);
        w.u16(uint16_t(n.triangleCount));
    }
    for (const TriangleRef& r : bvh.refs) {
        w.u16(r.objectIndex);
        w.u16(r.triangleIndex);
    }

    // ---- interactables (28 bytes)
    w.align4();
    for (const FlatObject* fo : interactables) {
        const InteractableComponent& it = *fo->object->interactable;
        // 2.4.0 needs a PSXObjectExporter on the same object; without a mesh
        // here the index is 0xFFFF.
        auto ex = std::find_if(exporters.begin(), exporters.end(),
                               [&](const ExpObject& e) { return e.obj == fo->object; });
        w.i32(toWorldFixed12(it.radius * it.radius / (gte * gte)));
        w.u8(uint8_t(it.button));
        w.u8(uint8_t((it.repeatable ? 1 : 0) | (it.showPrompt ? 2 : 0) | (it.lineOfSight ? 4 : 0)));
        w.u16(it.cooldownFrames);
        w.u16(0);  // current cooldown (runtime)
        w.u16(ex == exporters.end() ? 0xFFFF : uint16_t(ex - exporters.begin()));
        char name[16] = {};
        std::memcpy(name, it.promptCanvas.data(), std::min<size_t>(it.promptCanvas.size(), 15));
        for (char ch : name) w.u8(uint8_t(ch));
    }

    // ---- nav regions
    if (!nav.regions.empty()) {
        w.align4();
        writeNavRegions(w, nav, gte);
    }

    // ---- atlas + CLUT metadata (the reader skips the contents)
    for (Atlas& a : vram.atlases) {
        w.u32(0);
        w.u16(uint16_t(a.width));
        w.u16(uint16_t(Atlas::kHeight));
        w.u16(uint16_t(a.positionX));
        w.u16(uint16_t(a.positionY));
    }
    for (Atlas& a : vram.atlases)
        for (PsxTexture* t : a.textures) {
            if (!t->hasPalette) continue;
            w.u32(0);
            w.u16(t->clutPackingX);
            w.u16(t->clutPackingY);
            w.u16(uint16_t(t->palette.size()));
            w.u16(0);
        }

    // ---- Lua data
    for (size_t i = 0; i < luaData.size(); i++) {
        w.align4();
        w.patchU32(luaOffsetPos[i], uint32_t(w.pos()));
        w.bytes(luaData[i]);
    }

    // ---- mesh data (52-byte Tri)
    for (size_t oi = 0; oi < exporters.size(); oi++) {
        ExpObject& e = exporters[oi];
        w.align4();
        w.patchU32(meshOffsetPos[oi], uint32_t(w.pos()));
        for (const PsxTri& t : e.tris) {
            for (const PsxVert& v : t.v) {
                w.i16(v.vx);
                w.i16(v.vy);
                w.i16(v.vz);
            }
            w.i16(t.v[0].nx);
            w.i16(t.v[0].ny);
            w.i16(t.v[0].nz);
            for (const PsxVert& v : t.v) {
                w.u8(v.r);
                w.u8(v.g);
                w.u8(v.b);
                w.u8(0);
            }
            uint16_t flags = e.mc->uvOffsetMaterial == t.textureIndex ? 1 : 0;
            if (t.textureIndex < 0) {
                for (int i = 0; i < 6; i++) w.u8(0);
                w.u16(0);
                w.u16(0xFFFF);
                w.u16(0);
                w.u16(0);
                w.u16(flags);
            } else {
                const PsxTexture* tex = e.finalTextures[size_t(t.textureIndex)];
                int expander = tex->bitDepth == BitDepth::Bpp4 ? 4 : tex->bitDepth == BitDepth::Bpp8 ? 2 : 1;
                for (const PsxVert& v : t.v) {
                    w.u8(uint8_t(v.u + tex->packingX * expander));
                    w.u8(uint8_t(v.v + tex->packingY));
                }
                w.u16(0);
                uint16_t mode = tex->bitDepth == BitDepth::Bpp4 ? 0 : tex->bitDepth == BitDepth::Bpp8 ? 1 : 2;
                uint16_t tpage = uint16_t((tex->texpageX & 0xF) | ((tex->texpageY & 1) << 4) | (mode << 7) | 0x200);
                w.u16(tpage);
                w.u16(tex->clutPackingX);
                w.u16(tex->clutPackingY);
                w.u16(flags);
            }
        }
    }

    // ---- object name table
    w.align4();
    w.patchU32(nameTableOffsetPos, uint32_t(w.pos()));
    for (ExpObject& e : exporters) {
        std::string n = truncateUtf16(e.obj->name, 24);
        w.u8(uint8_t(n.size()));
        w.bytes(n);
        w.u8(0);
    }

    // ---- audio clip table (16 bytes each, then the names). The ADPCM goes in
    // the .spu file, so dataOffset stays 0. Names are UTF-8 (2.4.0: ASCII).
    if (!audioSources.empty()) {
        w.align4();
        w.patchU32(audioTableOffsetPos, uint32_t(w.pos()));
        std::vector<std::string> names;
        std::vector<size_t> nameOffsetPos;
        for (size_t i = 0; i < audioSources.size(); i++) {
            const AudioComponent& a = *audioSources[i]->object->audio;
            std::string n = a.clipName;
            if (n.size() > 255) {
                size_t cut = 255;
                while (cut > 0 && (uint8_t(n[cut]) & 0xC0) == 0x80) cut--;
                n.resize(cut);
            }
            w.u32(0);
            w.u32(uint32_t(audioData[i].size()));
            w.u16(uint16_t(a.sampleRate));
            w.u8(a.loop ? 1 : 0);
            w.u8(uint8_t(n.size()));
            nameOffsetPos.push_back(w.pos());
            w.u32(0);
            names.push_back(std::move(n));
        }
        for (size_t i = 0; i < names.size(); i++) {
            w.patchU32(nameOffsetPos[i], uint32_t(w.pos()));
            w.bytes(names[i]);
            w.u8(0);
        }
    }

    fs::path outPath = out;
    w.save(outPath);

    // ---- .vram
    BinWriter v;
    v.u8('V');
    v.u8('R');
    v.u16(uint16_t(vram.atlases.size()));
    v.u16(uint16_t(clutCount));
    v.u8(0);  // fonts
    v.u8(0);
    for (Atlas& a : vram.atlases) {
        v.u16(uint16_t(a.positionX));
        v.u16(uint16_t(a.positionY));
        v.u16(uint16_t(a.width));
        v.u16(uint16_t(Atlas::kHeight));
        for (uint16_t px : a.pixels) v.u16(px);
        v.align4();
    }
    for (Atlas& a : vram.atlases)
        for (PsxTexture* t : a.textures) {
            if (!t->hasPalette) continue;
            v.u16(t->clutPackingX);
            v.u16(t->clutPackingY);
            v.u16(uint16_t(t->palette.size()));
            v.u16(0);
            for (uint16_t c : t->palette) v.u16(c);
            v.align4();
        }
    v.save(fs::path(outPath).replace_extension(".vram"));

    // ---- .spu
    BinWriter s;
    s.u8('S');
    s.u8('A');
    s.u16(uint16_t(audioSources.size()));
    for (size_t i = 0; i < audioSources.size(); i++) {
        const AudioComponent& a = *audioSources[i]->object->audio;
        s.u32(uint32_t(audioData[i].size()));
        s.u16(uint16_t(a.sampleRate));
        s.u8(a.loop ? 1 : 0);
        s.u8(0);
        for (uint8_t b : audioData[i]) s.u8(b);
        s.align4();
    }
    s.save(fs::path(outPath).replace_extension(".spu"));
    return res;
}

}  // namespace splash
