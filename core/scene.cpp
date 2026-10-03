#include "scene.hh"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace splash {

// Floats parse with strtof and print as the shortest 32-bit round trip, so a
// value goes file -> float without passing through double (no double rounding).
using json = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, std::int64_t,
                                  std::uint64_t, float>;
namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

json readJson(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) fail("cannot open " + file.string());
    try {
        return json::parse(in);
    } catch (const json::exception& e) {
        fail(file.string() + ": " + e.what());
    }
}

float f(const json& j) { return j.get<float>(); }
Vec3 vec3(const json& j) { return {f(j.at(0)), f(j.at(1)), f(j.at(2))}; }
json vec3(Vec3 v) { return json::array({v.x, v.y, v.z}); }

template <typename E>
E enumFrom(const json& j, std::initializer_list<std::pair<const char*, E>> table) {
    std::string s = j.get<std::string>();
    for (auto& [name, value] : table)
        if (s == name) return value;
    fail("unknown value '" + s + "'");
}
template <typename E>
const char* enumTo(E v, std::initializer_list<std::pair<const char*, E>> table) {
    for (auto& [name, value] : table)
        if (v == value) return name;
    return "?";
}

const std::initializer_list<std::pair<const char*, VertexColorMode>> kColorModes = {
    {"baked", VertexColorMode::Baked}, {"flat", VertexColorMode::Flat}, {"mesh", VertexColorMode::Mesh}};
const std::initializer_list<std::pair<const char*, ColliderKind>> kColliderKinds = {
    {"none", ColliderKind::None}, {"static", ColliderKind::Static}, {"dynamic", ColliderKind::Dynamic}};
const std::initializer_list<std::pair<const char*, LightKind>> kLightKinds = {
    {"directional", LightKind::Directional}, {"point", LightKind::Point}, {"spot", LightKind::Spot}};
const std::initializer_list<std::pair<const char*, SceneType>> kSceneTypes = {
    {"exterior", SceneType::Exterior}, {"interior", SceneType::Interior}};
const std::initializer_list<std::pair<const char*, NavPartition>> kPartitions = {
    {"watershed", NavPartition::Watershed}, {"monotone", NavPartition::Monotone}, {"layer", NavPartition::Layer}};

void readNav(const json& c, NavBakeSettings& n) {
    n.maxStepHeight = c.value("maxStepHeight", n.maxStepHeight);
    n.walkableSlopeAngle = c.value("walkableSlopeAngle", n.walkableSlopeAngle);
    n.cellSize = c.value("navCellSize", n.cellSize);
    n.cellHeight = c.value("navCellHeight", n.cellHeight);
    n.minRegionArea = c.value("navMinRegionArea", n.minRegionArea);
    n.mergeRegionArea = c.value("navMergeRegionArea", n.mergeRegionArea);
    n.maxSimplifyError = c.value("navMaxSimplifyError", n.maxSimplifyError);
    n.maxEdgeLength = c.value("navMaxEdgeLength", n.maxEdgeLength);
    if (c.contains("navPartitionMethod")) n.partition = enumFrom(c["navPartitionMethod"], kPartitions);
    n.detailSampleDist = c.value("navDetailSampleDist", n.detailSampleDist);
    n.detailMaxError = c.value("navDetailMaxError", n.detailMaxError);
    n.maxPlaneError = c.value("navMaxPlaneError", n.maxPlaneError);
}

void writeNav(json& j, const NavBakeSettings& n) {
    j["maxStepHeight"] = n.maxStepHeight;
    j["walkableSlopeAngle"] = n.walkableSlopeAngle;
    j["navCellSize"] = n.cellSize;
    j["navCellHeight"] = n.cellHeight;
    j["navMinRegionArea"] = n.minRegionArea;
    j["navMergeRegionArea"] = n.mergeRegionArea;
    j["navMaxSimplifyError"] = n.maxSimplifyError;
    j["navMaxEdgeLength"] = n.maxEdgeLength;
    j["navPartitionMethod"] = enumTo(n.partition, kPartitions);
    j["navDetailSampleDist"] = n.detailSampleDist;
    j["navDetailMaxError"] = n.detailMaxError;
    j["navMaxPlaneError"] = n.maxPlaneError;
}

Object readObject(const json& j) {
    Object o;
    o.name = j.at("name").get<std::string>();
    o.active = j.value("active", true);
    if (j.contains("transform")) {
        const json& t = j["transform"];
        if (t.contains("position")) o.transform.position = vec3(t["position"]);
        if (t.contains("rotation")) {
            const json& r = t["rotation"];
            o.transform.rotation = {f(r.at(0)), f(r.at(1)), f(r.at(2)), f(r.at(3))};
        }
        if (t.contains("scale")) o.transform.scale = vec3(t["scale"]);
    }
    for (const json& c : j.value("components", json::array())) {
        std::string type = c.at("type").get<std::string>();
        if (type == "mesh") {
            MeshComponent m;
            m.mesh = c.at("mesh").get<std::string>();
            for (const json& mj : c.value("materials", json::array())) {
                Material mat;
                if (mj.contains("texture") && !mj["texture"].is_null()) mat.texture = mj["texture"].get<std::string>();
                if (mj.contains("color"))
                    for (int i = 0; i < 4; i++) mat.color[i] = f(mj["color"].at(i));
                m.materials.push_back(mat);
            }
            int bd = c.value("bitDepth", 8);
            if (bd != 4 && bd != 8 && bd != 16) fail("bitDepth must be 4, 8 or 16");
            m.bitDepth = BitDepth(bd);
            if (c.contains("vertexColors")) m.vertexColors = enumFrom(c["vertexColors"], kColorModes);
            if (c.contains("flatColor"))
                for (int i = 0; i < 3; i++) m.flatColor[i] = c["flatColor"].at(i).get<uint8_t>();
            m.smoothNormals = c.value("smoothNormals", true);
            m.uvOffsetMaterial = c.value("uvOffsetMaterial", 0);
            o.mesh = m;
        } else if (type == "collider") {
            ColliderComponent cc;
            if (c.contains("kind")) cc.kind = enumFrom(c["kind"], kColliderKinds);
            cc.platform = c.value("platform", false);
            o.collider = cc;
        } else if (type == "script") {
            o.script = ScriptComponent{c.at("lua").get<std::string>()};
        } else if (type == "light") {
            LightComponent l;
            if (c.contains("kind")) l.kind = enumFrom(c["kind"], kLightKinds);
            if (c.contains("color"))
                for (int i = 0; i < 3; i++) l.color[i] = f(c["color"].at(i));
            l.intensity = c.value("intensity", 1.f);
            l.range = c.value("range", 10.f);
            l.spotAngle = c.value("spotAngle", 30.f);
            l.innerSpotAngle = c.value("innerSpotAngle", 0.f);
            l.enabled = c.value("enabled", true);
            o.light = l;
        } else if (type == "player") {
            PlayerComponent pc;
            pc.playerHeight = c.value("playerHeight", pc.playerHeight);
            pc.playerRadius = c.value("playerRadius", pc.playerRadius);
            pc.moveSpeed = c.value("moveSpeed", pc.moveSpeed);
            pc.sprintSpeed = c.value("sprintSpeed", pc.sprintSpeed);
            readNav(c, pc.nav);
            pc.jumpHeight = c.value("jumpHeight", pc.jumpHeight);
            pc.gravity = c.value("gravity", pc.gravity);
            o.player = pc;
        } else if (type == "navigation") {
            NavigationComponent nc;
            nc.agentHeight = c.value("agentHeight", nc.agentHeight);
            nc.agentRadius = c.value("agentRadius", nc.agentRadius);
            readNav(c, nc.nav);
            if (c.contains("spawnAnchor") && !c["spawnAnchor"].is_null())
                nc.spawnAnchor = c["spawnAnchor"].get<std::string>();
            o.navigation = nc;
        } else {
            fail("object '" + o.name + "': unknown component type '" + type + "'");
        }
    }
    for (const json& cj : j.value("children", json::array())) o.children.push_back(readObject(cj));
    return o;
}

json writeObject(const Object& o) {
    json j;
    j["name"] = o.name;
    j["active"] = o.active;
    j["transform"] = {{"position", vec3(o.transform.position)},
                      {"rotation", json::array({o.transform.rotation.x, o.transform.rotation.y,
                                                o.transform.rotation.z, o.transform.rotation.w})},
                      {"scale", vec3(o.transform.scale)}};
    json comps = json::array();
    if (o.mesh) {
        const MeshComponent& m = *o.mesh;
        json mats = json::array();
        for (const Material& mat : m.materials) {
            json mj;
            mj["texture"] = mat.texture.empty() ? json(nullptr) : json(mat.texture);
            mj["color"] = json::array({mat.color[0], mat.color[1], mat.color[2], mat.color[3]});
            mats.push_back(mj);
        }
        comps.push_back({{"type", "mesh"},
                         {"mesh", m.mesh},
                         {"materials", mats},
                         {"bitDepth", int(m.bitDepth)},
                         {"vertexColors", enumTo(m.vertexColors, kColorModes)},
                         {"flatColor", json::array({m.flatColor[0], m.flatColor[1], m.flatColor[2]})},
                         {"smoothNormals", m.smoothNormals},
                         {"uvOffsetMaterial", m.uvOffsetMaterial}});
    }
    if (o.collider)
        comps.push_back({{"type", "collider"},
                         {"kind", enumTo(o.collider->kind, kColliderKinds)},
                         {"platform", o.collider->platform}});
    if (o.script) comps.push_back({{"type", "script"}, {"lua", o.script->lua}});
    if (o.light) {
        const LightComponent& l = *o.light;
        comps.push_back({{"type", "light"},
                         {"kind", enumTo(l.kind, kLightKinds)},
                         {"color", json::array({l.color[0], l.color[1], l.color[2]})},
                         {"intensity", l.intensity},
                         {"range", l.range},
                         {"spotAngle", l.spotAngle},
                         {"innerSpotAngle", l.innerSpotAngle},
                         {"enabled", l.enabled}});
    }
    if (o.player) {
        const PlayerComponent& pc = *o.player;
        json pj = {{"type", "player"},
                   {"playerHeight", pc.playerHeight},
                   {"playerRadius", pc.playerRadius},
                   {"moveSpeed", pc.moveSpeed},
                   {"sprintSpeed", pc.sprintSpeed}};
        writeNav(pj, pc.nav);
        pj["jumpHeight"] = pc.jumpHeight;
        pj["gravity"] = pc.gravity;
        comps.push_back(pj);
    }
    if (o.navigation) {
        const NavigationComponent& nc = *o.navigation;
        json nj = {{"type", "navigation"}, {"agentHeight", nc.agentHeight}, {"agentRadius", nc.agentRadius}};
        writeNav(nj, nc.nav);
        nj["spawnAnchor"] = nc.spawnAnchor.empty() ? json(nullptr) : json(nc.spawnAnchor);
        comps.push_back(nj);
    }
    j["components"] = comps;
    json kids = json::array();
    for (const Object& c : o.children) kids.push_back(writeObject(c));
    j["children"] = kids;
    return j;
}

void writeJson(const json& j, const fs::path& file) {
    std::ofstream out(file, std::ios::binary);
    if (!out) fail("cannot write " + file.string());
    out << j.dump(2) << '\n';
}

Quat mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
            a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

void flattenInto(const Object& o, const FlatObject* parent, std::vector<FlatObject>& out) {
    FlatObject fo;
    fo.object = &o;
    const Transform& t = o.transform;
    if (!parent) {
        fo.worldPosition = t.position;
        fo.worldRotation = t.rotation;
        fo.lossyScale = t.scale;
        fo.activeInHierarchy = true;
    } else {
        // Nested transforms are not yet checked against Unity's composition.
        fo.worldPosition = parent->localToWorld.point(t.position);
        fo.worldRotation = mul(parent->worldRotation, t.rotation);
        fo.lossyScale = scale(parent->lossyScale, t.scale);
        fo.activeInHierarchy = parent->activeInHierarchy;
    }
    fo.localToWorld = Mat34::trs(fo.worldPosition, fo.worldRotation, fo.lossyScale);
    out.push_back(fo);
    FlatObject self = out.back();
    for (const Object& c : o.children) flattenInto(c, &self, out);
}

}  // namespace

std::vector<FlatObject> flatten(const Scene& scene) {
    std::vector<FlatObject> out;
    for (const Object& o : scene.objects) flattenInto(o, nullptr, out);
    return out;
}

Scene loadScene(const fs::path& file) {
    json j = readJson(file);
    if (j.value("format", "") != "splashedit-ng/scene") fail(file.string() + ": not a splashedit-ng scene");
    Scene s;
    if (j.contains("settings")) {
        const json& st = j["settings"];
        s.settings.gteScaling = st.value("gteScaling", 100.f);
        if (st.contains("sceneType")) s.settings.sceneType = enumFrom(st["sceneType"], kSceneTypes);
        if (st.contains("fog")) {
            const json& fg = st["fog"];
            s.settings.fog.enabled = fg.value("enabled", false);
            if (fg.contains("color"))
                for (int i = 0; i < 3; i++) s.settings.fog.color[i] = f(fg["color"].at(i));
            s.settings.fog.density = fg.value("density", 5);
        }
        s.settings.networkId = st.value("networkId", "");
        if (st.contains("script") && !st["script"].is_null()) s.settings.script = st["script"].get<std::string>();
    }
    for (const json& oj : j.value("objects", json::array())) s.objects.push_back(readObject(oj));
    return s;
}

void saveScene(const Scene& s, const fs::path& file) {
    json j;
    j["format"] = "splashedit-ng/scene";
    j["version"] = 1;
    j["settings"] = {{"gteScaling", s.settings.gteScaling},
                     {"sceneType", enumTo(s.settings.sceneType, kSceneTypes)},
                     {"fog",
                      {{"enabled", s.settings.fog.enabled},
                       {"color", json::array({s.settings.fog.color[0], s.settings.fog.color[1], s.settings.fog.color[2]})},
                       {"density", s.settings.fog.density}}},
                     {"networkId", s.settings.networkId},
                     {"script", s.settings.script.empty() ? json(nullptr) : json(s.settings.script)}};
    json objs = json::array();
    for (const Object& o : s.objects) objs.push_back(writeObject(o));
    j["objects"] = objs;
    writeJson(j, file);
}

Bounds Mesh::bounds() const {
    if (positions.empty()) return {};
    Vec3 mn = positions[0], mx = positions[0];
    for (const Vec3& p : positions) {
        mn = vmin(mn, p);
        mx = vmax(mx, p);
    }
    Bounds b;
    b.center = (mn + mx) * 0.5f;
    b.extents = (mx - mn) * 0.5f;
    return b;
}

Mesh loadMesh(const fs::path& file) {
    json j = readJson(file);
    if (j.value("format", "") != "splashedit-ng/mesh") fail(file.string() + ": not a splashedit-ng mesh");
    Mesh m;
    auto floats = [&](const char* key) {
        std::vector<float> v;
        if (j.contains(key))
            for (const json& x : j[key]) v.push_back(x.get<float>());
        return v;
    };
    auto p = floats("positions"), n = floats("normals"), uv = floats("uv"), c = floats("colors");
    for (size_t i = 0; i + 2 < p.size(); i += 3) m.positions.push_back({p[i], p[i + 1], p[i + 2]});
    for (size_t i = 0; i + 2 < n.size(); i += 3) m.normals.push_back({n[i], n[i + 1], n[i + 2]});
    for (size_t i = 0; i + 1 < uv.size(); i += 2) m.uv.push_back({uv[i], uv[i + 1]});
    for (size_t i = 0; i + 3 < c.size(); i += 4) m.colors.push_back({c[i], c[i + 1], c[i + 2], c[i + 3]});
    for (const json& sm : j.value("submeshes", json::array())) m.submeshes.push_back(sm.get<std::vector<int>>());
    size_t nv = m.positions.size();
    if (!m.normals.empty() && m.normals.size() != nv) fail(file.string() + ": normals count mismatch");
    if (!m.uv.empty() && m.uv.size() != nv) fail(file.string() + ": uv count mismatch");
    for (auto& sm : m.submeshes) {
        if (sm.size() % 3) fail(file.string() + ": submesh index count not a multiple of 3");
        for (int i : sm)
            if (i < 0 || size_t(i) >= nv) fail(file.string() + ": index out of range");
    }
    return m;
}

void saveMesh(const Mesh& m, const fs::path& file) {
    json j;
    j["format"] = "splashedit-ng/mesh";
    j["version"] = 1;
    json p = json::array(), n = json::array(), uv = json::array(), c = json::array();
    for (auto& v : m.positions) p.insert(p.end(), {v.x, v.y, v.z});
    for (auto& v : m.normals) n.insert(n.end(), {v.x, v.y, v.z});
    for (auto& v : m.uv) uv.insert(uv.end(), {v.x, v.y});
    for (auto& v : m.colors) c.insert(c.end(), {v[0], v[1], v[2], v[3]});
    j["positions"] = p;
    if (!m.normals.empty()) j["normals"] = n;
    if (!m.uv.empty()) j["uv"] = uv;
    if (!m.colors.empty()) j["colors"] = c;
    j["submeshes"] = m.submeshes;
    writeJson(j, file);
}

}  // namespace splash
