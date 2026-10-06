// Headless tests for the editor's document model: the undo history and the
// viewport's ray picker and gizmo maths. No SDL, no GL. Run through CTest (`ctest`).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <system_error>

#include "editor/catalog.hh"
#include "editor/document.hh"
#include "editor/gizmo.hh"
#include "editor/live_export.hh"
#include "editor/pick.hh"
#include "editor/play.hh"
#include "editor/project.hh"
#include "budget.hh"
#include "splashpack.hh"

namespace {

int g_failures = 0;
int g_checks = 0;
std::filesystem::path g_outDir;  // next to the executable, inside the build tree

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++g_checks;                                                                \
        if (!(cond)) {                                                             \
            ++g_failures;                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                          \
    } while (0)

splash::Object named(const char* name, float x = 0) {
    splash::Object o;
    o.name = name;
    o.transform.position = {x, 0, 0};
    return o;
}

// Scene: A, B (with child B1), C.
splash::Scene sample() {
    splash::Scene s;
    s.objects.push_back(named("A", 1));
    splash::Object b = named("B", 2);
    b.children.push_back(named("B1", 3));
    s.objects.push_back(b);
    s.objects.push_back(named("C", 4));
    return s;
}

void setX(editor::Document& d, const editor::ObjectPath& p, float x, const std::string& key) {
    d.edit(p, [&](splash::Object& o) { o.transform.position.x = x; }, key);
}

void testApplyUndoRedo() {
    editor::Document d;
    d.reset(sample());
    CHECK(!d.dirty());
    CHECK(!d.canUndo() && !d.canRedo());
    unsigned rev = d.revision();
    setX(d, {0}, 10, "");
    CHECK(d.object({0})->transform.position.x == 10);
    CHECK(d.revision() != rev);
    CHECK(d.dirty());
    CHECK(d.canUndo());
    // The edit leaves children alone.
    setX(d, {1}, 20, "");
    CHECK(d.object({1, 0}) && d.object({1, 0})->name == "B1");
    rev = d.revision();
    CHECK(d.undo());
    CHECK(d.revision() != rev);
    CHECK(d.object({1})->transform.position.x == 2);
    CHECK(d.object({1, 0}) && d.object({1, 0})->name == "B1");
    CHECK(d.undo());
    CHECK(d.object({0})->transform.position.x == 1);
    CHECK(!d.dirty());
    CHECK(!d.undo());
    CHECK(d.redo());
    CHECK(d.object({0})->transform.position.x == 10);
    CHECK(d.dirty());
    // A new edit drops what could be redone.
    setX(d, {2}, 40, "");
    CHECK(!d.canRedo());
    CHECK(d.historySize() == 2);
}

void testMerge() {
    editor::Document d;
    d.reset(sample());
    // A drag: many updates with one key, one undo step.
    for (int i = 1; i <= 5; ++i) setX(d, {0}, 1.0f + static_cast<float>(i), "drag");
    CHECK(d.historySize() == 1);
    CHECK(d.object({0})->transform.position.x == 6);
    d.endMerge();
    // A second drag on the same field is its own step.
    for (int i = 1; i <= 3; ++i) setX(d, {0}, 6.0f + static_cast<float>(i), "drag");
    CHECK(d.historySize() == 2);
    CHECK(d.undo());
    CHECK(d.object({0})->transform.position.x == 6);
    CHECK(d.undo());
    CHECK(d.object({0})->transform.position.x == 1);
    CHECK(!d.dirty());
    CHECK(d.redo() && d.redo());
    CHECK(d.object({0})->transform.position.x == 9);

    // Different object or different key: no merge, even while a merge is open.
    editor::Document e;
    e.reset(sample());
    setX(e, {0}, 5, "drag");
    setX(e, {2}, 5, "drag");
    CHECK(e.historySize() == 2);
    setX(e, {2}, 6, "other");
    CHECK(e.historySize() == 3);
    // An edit without a key never merges.
    setX(e, {2}, 7, "");
    setX(e, {2}, 8, "");
    CHECK(e.historySize() == 5);
    // Undo closes the merge: the next keyed edit is a new step.
    setX(e, {1}, 1, "k");
    CHECK(e.undo());
    setX(e, {1}, 2, "k");
    setX(e, {1}, 3, "k");
    CHECK(e.historySize() == 6);
    CHECK(e.undo());
    CHECK(e.object({1})->transform.position.x == 2);
}

// Scene settings edit through the same history as objects.
void testSettings() {
    editor::Document d;
    d.reset(sample());
    unsigned rev = d.revision();
    for (int v : {300, 400, 500}) d.editSettings([&](splash::SceneSettings& s) { s.orderingTableSize = v; }, "ot");
    CHECK(d.historySize() == 1);
    CHECK(d.revision() > rev);
    CHECK(d.dirty());
    d.endMerge();
    d.editSettings([](splash::SceneSettings& s) { s.orderingTableSize = 0; });
    CHECK(d.scene().settings.orderingTableSize == 0);
    CHECK(d.undo());
    CHECK(d.scene().settings.orderingTableSize == 500);
    CHECK(d.undo());
    CHECK(d.scene().settings.orderingTableSize == 0);
    CHECK(!d.dirty());
    CHECK(d.redo());
    CHECK(d.scene().settings.orderingTableSize == 500);
}

void testRenderPeak() {
    editor::RenderPeak p;
    CHECK(editor::parseRenderPeak("psxsplash: render peak depth 258 of 988, bump 11808 of 51656", &p));
    CHECK(p.depth == 258 && p.orderingTable == 988 && p.bump == 11808 && p.bumpSize == 51656);
    CHECK(editor::parseRenderPeak("[tty] psxsplash: render peak depth 3 of 256, bump 0 of 4000", &p) && p.bumpSize == 4000);
    CHECK(!editor::parseRenderPeak("psxsplash: render peak depth", &p));
    CHECK(!editor::parseRenderPeak("Loading scene 0", &p));
}

void testDirtyAndSave() {
    std::error_code ec;
    std::filesystem::path dir = g_outDir;
    std::filesystem::create_directories(dir, ec);
    editor::Document d;
    d.reset(sample(), dir, "saved");
    setX(d, {0}, 3, "");
    CHECK(d.dirty());
    CHECK(!d.save());
    CHECK(!d.dirty());
    CHECK(std::filesystem::is_regular_file(dir / "saved.scene"));
    setX(d, {0}, 4, "");
    CHECK(d.dirty());
    CHECK(d.undo());
    CHECK(!d.dirty());  // back at the saved state
    CHECK(d.undo());
    CHECK(d.dirty());  // before the saved state
    CHECK(d.redo());
    CHECK(!d.dirty());

    // Merging into the saved step makes the document dirty again.
    editor::Document m;
    m.reset(sample(), dir, "saved2");
    setX(m, {0}, 3, "drag");
    CHECK(!m.save());
    setX(m, {0}, 4, "drag");  // save closed the merge, so this is a new step
    CHECK(m.historySize() == 2);
    CHECK(m.dirty());

    // Reloading what was saved gives the edited value.
    editor::Document r;
    CHECK(!r.load(dir, "saved.scene"));
    CHECK(r.object({0}) && r.object({0})->transform.position.x == 3);
    CHECK(!r.dirty() && !r.canUndo());
}

void testStructure() {
    editor::Document d;
    d.reset(sample());
    d.select(editor::ObjectPath{2});  // C
    CHECK(d.removeObject({0}));       // A
    CHECK(d.scene().objects.size() == 2);
    CHECK(d.objectCount() == 3);
    CHECK(d.selected() && d.selected()->name == "C");  // the selection follows C to index 1
    CHECK(d.undo());
    CHECK(d.scene().objects.size() == 3 && d.object({0})->name == "A");
    CHECK(d.objectCount() == 4);

    // Delete the selected object; undo brings it back with its children.
    d.select(editor::ObjectPath{1});
    CHECK(d.removeObject({1}));
    CHECK(!d.selection());
    CHECK(d.undo());
    CHECK(d.object({1})->name == "B" && d.object({1, 0}) && d.object({1, 0})->name == "B1");

    // Duplicate: inserted after the original, numbered, selected, undoable.
    auto p = d.duplicateObject({1});
    CHECK(p && *p == editor::ObjectPath({2}));
    CHECK(d.object({2})->name == "B (2)" && d.object({2, 0}) && d.object({2, 0})->name == "B1");
    CHECK(d.object({3})->name == "C");
    CHECK(d.selected() == d.object({2}));
    auto q = d.duplicateObject({2});
    CHECK(q && d.object(*q)->name == "B (3)");
    CHECK(d.undo() && d.undo());
    CHECK(d.scene().objects.size() == 3 && d.object({2})->name == "C");
    CHECK(d.redo());
    CHECK(d.object({2})->name == "B (2)");

    // Rename through edit().
    d.edit({0}, [](splash::Object& o) { o.name = "Renamed"; });
    CHECK(d.object({0})->name == "Renamed");
    CHECK(d.undo());
    CHECK(d.object({0})->name == "A");

    // Flatten order paths.
    std::vector<editor::ObjectPath> fp = d.flatPaths();
    std::vector<splash::FlatObject> flats = splash::flatten(d.scene());
    CHECK(fp.size() == flats.size());
    for (size_t i = 0; i < fp.size() && i < flats.size(); ++i) CHECK(d.object(fp[i]) == flats[i].object);
}

// An axis-aligned box as 12 triangles.
void addBox(editor::PickMesh& m, int object, splash::Vec3 c, float h) {
    splash::Vec3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = {c.x + (i & 1 ? h : -h), c.y + (i & 2 ? h : -h), c.z + (i & 4 ? h : -h)};
    const int f[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    size_t first = m.positions.size() / 3;
    for (auto& q : f) {
        m.positions.insert(m.positions.end(), {v[q[0]], v[q[1]], v[q[2]]});
        m.positions.insert(m.positions.end(), {v[q[0]], v[q[2]], v[q[3]]});
    }
    m.ranges.push_back({object, first, 12});
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testPick() {
    editor::PickMesh m;
    addBox(m, 7, {0, 0, 0}, 1);
    // Hit: straight down the Z axis from z = -5 hits the near face at z = -1.
    auto hit = editor::pickNearest({{0, 0, -5}, {0, 0, 1}}, m);
    CHECK(hit && hit->object == 7 && near(hit->t, 4));
    // Off-centre and oblique still hit.
    hit = editor::pickNearest({{0.9f, -0.9f, -5}, {0, 0, 1}}, m);
    CHECK(hit && hit->object == 7);
    hit = editor::pickNearest({{-4, 3, -4}, {1, -0.75f, 1}}, m);
    CHECK(hit && hit->object == 7);
    // Miss: beside the box, and pointing away from it.
    CHECK(!editor::pickNearest({{1.5f, 0, -5}, {0, 0, 1}}, m));
    CHECK(!editor::pickNearest({{0, 0, -5}, {0, 0, -1}}, m));
    CHECK(!editor::pickNearest({{0, 2, -5}, {0, 0, 1}}, m));
    // From inside the box the far side counts (both faces).
    hit = editor::pickNearest({{0, 0, 0}, {0, 0, 1}}, m);
    CHECK(hit && near(hit->t, 1));

    // Nearest of two: a second box behind the first along the ray, added
    // first and last so the order in the soup cannot decide.
    editor::PickMesh two;
    addBox(two, 2, {0, 0, 6}, 1);
    addBox(two, 1, {0, 0, 0}, 1);
    hit = editor::pickNearest({{0, 0, -5}, {0, 0, 1}}, two);
    CHECK(hit && hit->object == 1 && near(hit->t, 4));
    hit = editor::pickNearest({{0, 0, 12}, {0, 0, -1}}, two);
    CHECK(hit && hit->object == 2 && near(hit->t, 5));
    // A ray that only passes the far box.
    hit = editor::pickNearest({{0, 5, 6}, {0, -1, 0}}, two);
    CHECK(hit && hit->object == 2);

    // Sphere (scene icons).
    auto s = editor::raySphere({{0, 0, -5}, {0, 0, 1}}, {0, 0, 0}, 0.5f);
    CHECK(s && near(*s, 4.5f));
    CHECK(!editor::raySphere({{1, 0, -5}, {0, 0, 1}}, {0, 0, 0}, 0.5f));
}

bool sameQuat(splash::Quat a, splash::Quat b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
bool nearV(splash::Vec3 a, splash::Vec3 b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

// The pure maths behind the rotate and scale gizmos.
void testGizmoMath() {
    const float pi = editor::kPi;
    auto nearF = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    // Quaternion.AngleAxis(90, up) * forward == right, as in Unity.
    splash::Quat q = editor::quatAxisAngle({0, 1, 0}, pi / 2);
    CHECK(nearV(splash::rotate(q, {0, 0, 1}), {1, 0, 0}));
    // The axis need not be unit length; a zero axis is the identity.
    CHECK(nearV(splash::rotate(editor::quatAxisAngle({0, 0, 5}, pi / 2), {1, 0, 0}), {0, 1, 0}));
    CHECK(sameQuat(editor::quatAxisAngle({0, 0, 0}, 1), splash::Quat{}));
    // a * b applies b first: 90 about Y then 90 about X takes +Z to +X then keeps it.
    splash::Quat x90 = editor::quatAxisAngle({1, 0, 0}, pi / 2);
    CHECK(nearV(splash::rotate(editor::quatMul(x90, q), {0, 0, 1}), {1, 0, 0}));
    CHECK(nearV(splash::rotate(editor::quatMul(q, x90), {0, 1, 0}), {1, 0, 0}));
    splash::Quat n = editor::quatNormalize({0, 0, 0, 2});
    CHECK(sameQuat(n, splash::Quat{}));
    CHECK(sameQuat(editor::quatNormalize({0, 0, 0, 0}), splash::Quat{}));

    // Mouse angle around the centre, y down: right is 0, below is +90.
    CHECK(nearF(editor::screenAngle(100, 100, 150, 100), 0));
    CHECK(nearF(editor::screenAngle(100, 100, 100, 160), pi / 2));
    // Steps go the short way across the +-180 seam, so a drag can pass it.
    CHECK(nearF(editor::angleStep(pi - 0.1f, -pi + 0.1f), 0.2f));
    CHECK(nearF(editor::angleStep(-pi + 0.1f, pi - 0.1f), -0.2f));
    CHECK(nearF(editor::angleStep(0.5f, 0.2f), -0.3f));
    float acc = 0, last = 0;
    for (int i = 1; i <= 40; ++i) {  // a full turn and a bit, in 10 degree steps
        float a = std::remainder(i * pi / 18, 2 * pi);
        acc += editor::angleStep(last, a);
        last = a;
    }
    CHECK(nearF(acc, 40 * pi / 18));

    // Snapping.
    CHECK(editor::snapTo(0.37f, 0.25f) == 0.25f);
    CHECK(editor::snapTo(0.38f, 0.25f) == 0.5f);
    CHECK(editor::snapTo(0.37f, 0) == 0.37f);
    CHECK(nearF(editor::rotateDragDegrees(pi / 4, false), 45));
    CHECK(editor::rotateDragDegrees(23.0f * pi / 180, true) == 30);
    CHECK(editor::rotateDragDegrees(-22.0f * pi / 180, true) == -15);
    CHECK(editor::rotateDragDegrees(7.0f * pi / 180, true) == 0);
    CHECK(nearF(editor::scaleAxis(1, 1.234f, true), 1.2f));
    CHECK(nearF(editor::scaleAxis(1, 1.234f, false), 1.234f));
    CHECK(nearF(editor::scaleAxis(2, 0.5f, false), 1));
}

// A scale gizmo can never reach zero or flip an object.
void testScaleClamp() {
    using editor::kMinScale;
    // Dragged through zero and beyond: held at the minimum, still positive.
    CHECK(editor::scaleAxis(1, 0, false) == kMinScale);
    CHECK(editor::scaleAxis(1, -3, false) == kMinScale);
    CHECK(editor::scaleAxis(1, 0.001f, false) == kMinScale);
    // Snapping a tiny value to 0 does not reach zero either.
    CHECK(editor::scaleAxis(1, 0.04f, true) == kMinScale);
    CHECK(editor::scaleAxis(0.5f, 0.05f, true) == kMinScale);
    // Above the minimum it is left alone.
    CHECK(editor::scaleAxis(1, 0.02f, false) == 0.02f);
    // An object already mirrored stays mirrored and away from zero.
    CHECK(editor::scaleAxis(-2, 0.5f, false) == -1);
    CHECK(editor::scaleAxis(-2, -1, false) == -kMinScale);
    CHECK(editor::scaleAxis(-2, 0, false) == -kMinScale);
    // A zero start recovers to the minimum rather than staying collapsed.
    CHECK(editor::scaleAxis(0, 3, false) == kMinScale);
    CHECK(editor::clampScale(std::nanf(""), 1) == kMinScale);
    // Uniform: per component, each keeping its sign; snap scales the factor.
    splash::Vec3 u = editor::scaleUniform({1, 2, -0.5f}, -1, false);
    CHECK(u.x == kMinScale && u.y == kMinScale && u.z == -kMinScale);
    u = editor::scaleUniform({1, 2, 0.5f}, 1.13f, true);
    CHECK(nearV(u, {1.1f, 2.2f, 0.55f}));
}

// One gizmo drag (many merged edits) undoes to the exact transform before it
// and redoes to the exact transform after it, for rotate and for scale.
void testGizmoUndo() {
    editor::Document d;
    splash::Scene s = sample();
    s.objects[0].transform.rotation = editor::quatAxisAngle({0.3f, 1, 0.2f}, 0.7f);
    s.objects[0].transform.scale = {1.5f, 0.75f, 2};
    d.reset(s);
    const splash::Transform before = d.object({0})->transform;

    // A rotate drag: a run of frames about world Y, as the gizmo edits.
    splash::Quat last;
    for (int f = 1; f <= 8; ++f) {
        float deg = editor::rotateDragDegrees(f * 0.05f, true);
        last = editor::quatNormalize(editor::quatMul(editor::quatAxisAngle({0, 1, 0}, deg * editor::kPi / 180), before.rotation));
        d.edit({0}, [&](splash::Object& o) { o.transform.rotation = last; }, "gizmo.rotate");
    }
    d.endMerge();
    CHECK(d.historySize() == 1);
    CHECK(sameQuat(d.object({0})->transform.rotation, last));
    CHECK(!sameQuat(last, before.rotation));
    CHECK(d.undo());
    CHECK(sameQuat(d.object({0})->transform.rotation, before.rotation));
    CHECK(d.object({0})->transform.scale == before.scale && d.object({0})->transform.position == before.position);
    CHECK(d.redo());
    CHECK(sameQuat(d.object({0})->transform.rotation, last));

    // A scale drag on X, then a uniform one: two drags, two steps.
    const splash::Transform mid = d.object({0})->transform;
    splash::Vec3 sx = mid.scale;
    for (int f = 1; f <= 6; ++f) {
        sx.x = editor::scaleAxis(mid.scale.x, 1 + f * 0.1f, false);
        d.edit({0}, [&](splash::Object& o) { o.transform.scale = sx; }, "gizmo.scale");
    }
    d.endMerge();
    splash::Vec3 su;
    for (int f = 1; f <= 6; ++f) {
        su = editor::scaleUniform(sx, 1 - f * 0.3f, false);  // past zero: clamped
        d.edit({0}, [&](splash::Object& o) { o.transform.scale = su; }, "gizmo.scale");
    }
    d.endMerge();
    CHECK(d.historySize() == 3);
    CHECK(su.x == editor::kMinScale && su.y == editor::kMinScale && su.z == editor::kMinScale);
    CHECK(d.object({0})->transform.scale == su);
    CHECK(d.undo());
    CHECK(d.object({0})->transform.scale == sx);
    CHECK(d.undo());
    CHECK(d.object({0})->transform.scale == mid.scale);
    CHECK(sameQuat(d.object({0})->transform.rotation, last));
    CHECK(d.undo());
    CHECK(sameQuat(d.object({0})->transform.rotation, before.rotation));
    CHECK(d.object({0})->transform.scale == before.scale);
    CHECK(!d.dirty());
    CHECK(d.redo() && d.redo());
    CHECK(d.object({0})->transform.scale == sx);
    CHECK(d.redo());
    CHECK(d.object({0})->transform.scale == su);

    // A rotate drag right after a move drag does not merge into it.
    d.reset(sample());
    setX(d, {0}, 5, "gizmo.move");
    d.edit({0}, [&](splash::Object& o) { o.transform.rotation = last; }, "gizmo.rotate");
    CHECK(d.historySize() == 2);
}

// What the Add object / Add component pickers offer, and how search ranks it.
void testCatalog() {
    using editor::ComponentKind;
    std::vector<std::pair<const char*, const char*>> items;
    for (const editor::ObjectPreset& p : editor::presets()) items.push_back({p.label, p.keywords});
    auto first = [&](const char* q) {
        std::vector<int> r = editor::rank(q, items);
        return r.empty() ? std::string() : std::string(editor::presets()[r[0]].label);
    };
    CHECK(editor::rank("", items).size() == items.size());
    CHECK(editor::rank("   ", items).size() == items.size());
    CHECK(first("") == "Empty");               // empty query keeps the list order
    CHECK(first("lig") == "Point light");      // word prefix inside the label
    CHECK(first("POINT") == "Point light");    // case-insensitive
    CHECK(first("sfx") == "Audio clip");       // keyword
    CHECK(first("tbx") == "Trigger box");      // letters in order
    CHECK(first("Inter") == "Interactable");
    CHECK(editor::rank("zzq", items).empty()); // nothing matches
    // A label prefix outranks a keyword hit: "mesh" is the Mesh preset, not one tagged "model".
    CHECK(first("mesh") == "Mesh");

    // Components: dependencies, cascade on removal, no double add.
    splash::Object o;
    o.name = "Crate";
    CHECK(editor::cannotAdd(o, ComponentKind::Collider) == "Needs a Mesh");
    CHECK(!editor::addComponent(o, ComponentKind::Collider) && !o.collider);
    CHECK(!editor::addComponent(o, ComponentKind::Skin) && !o.skin);
    CHECK(editor::addComponent(o, ComponentKind::Mesh) && o.mesh);
    CHECK(editor::cannotAdd(o, ComponentKind::Mesh) == "Already added");
    CHECK(!editor::addComponent(o, ComponentKind::Mesh));
    CHECK(editor::addComponent(o, ComponentKind::Collider));
    CHECK(o.collider && o.collider->kind == splash::ColliderKind::Static);
    CHECK(editor::addComponent(o, ComponentKind::Skin) && o.skin);
    CHECK(editor::addComponent(o, ComponentKind::Audio) && o.audio && o.audio->clipName == "Crate");
    CHECK(editor::removeComponent(o, ComponentKind::Mesh));
    CHECK(!o.mesh && !o.collider && !o.skin && o.audio);
    CHECK(!editor::removeComponent(o, ComponentKind::Mesh));
    for (const editor::ComponentInfo& c : editor::components()) {
        splash::Object m;
        m.mesh.emplace();
        if (c.kind != ComponentKind::Mesh) CHECK(editor::addComponent(m, c.kind) && editor::hasComponent(m, c.kind));
        CHECK(editor::removeComponent(m, c.kind) && !editor::hasComponent(m, c.kind));
    }

    // New objects take a free Unity-style name among their siblings.
    splash::Scene sc = sample();
    sc.objects.push_back(named("Point light"));
    const editor::ObjectPreset& light = editor::presets()[2];
    CHECK(std::string(light.label) == "Point light");
    splash::Object l2 = editor::makeObject(light, sc.objects);
    CHECK(l2.name == "Point light (2)" && l2.light && !l2.mesh);
    sc.objects.push_back(l2);
    CHECK(editor::uniqueName("Point light", sc.objects) == "Point light (3)");
    CHECK(editor::uniqueName("Empty", sc.objects) == "Empty");

    // Through the document: one undo step each way.
    editor::Document d;
    d.reset(sample());
    CHECK(d.edit({0}, [](splash::Object& ob) { editor::addComponent(ob, ComponentKind::Trigger); }));
    CHECK(d.object({0})->trigger);
    CHECK(d.undo() && !d.object({0})->trigger);
    CHECK(d.insertObject({3}, editor::makeObject(editor::presets()[0], d.scene().objects)));
    CHECK(d.object({3}) && d.object({3})->name == "Empty" && d.selection() == editor::ObjectPath{3});
    CHECK(d.undo() && !d.object({3}));
}

// Export stats: a dry run writes nothing and reports the sizes a real export
// writes; SPU use follows psxsplash's allocator, recomputed here from the .spu.
void testExportStats() {
    namespace fs = std::filesystem;
    const fs::path src = SPLASHEDIT_SOURCE_DIR;
    const fs::path project = src / "examples" / "courtyard";
    splash::Scene scene = splash::loadScene(project / "courtyard.scene");
    for (const char* clip : {"short11.wav", "sine22.wav"}) {
        splash::Object o = named(clip);
        splash::AudioComponent a;
        a.clip = (fs::path("..") / ".." / "tests" / "audio" / clip).generic_string();
        a.clipName = clip;
        a.sampleRate = 11025;
        o.audio = a;
        scene.objects.push_back(o);
    }
    const fs::path dir = g_outDir / "stats";
    fs::remove_all(dir);
    fs::create_directories(dir);
    splash::ExportOptions dry;
    dry.dryRun = true;
    splash::ExportResult d = splash::exportSplashpack(scene, project, dir / "scene.splashpack", dry);
    CHECK(d.ok());
    CHECK(fs::is_empty(dir));
    splash::ExportResult r = splash::exportSplashpack(scene, project, dir / "scene.splashpack");
    CHECK(r.ok());
    std::error_code ec;
    CHECK(d.stats.splashpackBytes == fs::file_size(dir / "scene.splashpack", ec));
    CHECK(d.stats.vramFileBytes == fs::file_size(dir / "scene.vram", ec));
    CHECK(d.stats.spuFileBytes == fs::file_size(dir / "scene.spu", ec));
    CHECK(d.stats.framebufferBytes == 320 * 240 * 2 * 2);
    CHECK(d.stats.atlasBytes > 0 && d.stats.triangles > 0);

    // .spu: "SA", u16 count, then per clip u32 size, u16 rate, u8 loop, u8 0, data, align 4.
    std::ifstream in(dir / "scene.spu", std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t end = 0x1010, p = 4;
    int clips = 0;
    while (p + 8 <= b.size()) {
        size_t n = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | size_t(b[p + 3]) << 24;
        end = (end + 15) / 16 * 16 + (n + 15) / 16 * 16;
        p += (8 + n + 3) / 4 * 4;
        clips++;
    }
    CHECK(clips == 2);
    CHECK(d.stats.spuEnd == end);
    CHECK(splash::spuBudget(d.stats).used == end);
    // The visible-triangle list is sized from the header's bvhTriangleRefCount
    // (u16 at offset 34), read back from the written file.
    {
        std::ifstream f(dir / "scene.splashpack", std::ios::binary);
        unsigned char h[36] = {};
        f.read(reinterpret_cast<char*>(h), sizeof h);
        CHECK(f.good());
        uint32_t refs = h[34] | h[35] << 8;
        CHECK(refs > 0);
        CHECK(d.stats.bvhTriangleRefs == refs);
        CHECK(d.stats.visibleListBytes() == 4 * size_t(refs));
    }
    CHECK(splash::ramBudget(d.stats).used == d.stats.rendererBytes() + d.stats.visibleListBytes() +
                                                 std::max({d.stats.splashpackBytes, d.stats.vramFileBytes,
                                                           d.stats.spuFileBytes}));

    // Overrides replace the estimate in the pack and in the RAM meter; an
    // ordering table below the minimum is raised to it.
    CHECK(d.stats.orderingTableSize == d.stats.orderingTableNeed && d.stats.bumpAllocatorSize == d.stats.bumpAllocatorNeed);
    scene.settings.orderingTableSize = 10;
    scene.settings.bumpAllocatorSize = 4001;
    splash::ExportResult o = splash::exportSplashpack(scene, project, dir / "scene.splashpack", dry);
    CHECK(o.ok());
    CHECK(o.stats.orderingTableSize == splash::kOtMin);
    CHECK(o.stats.bumpAllocatorSize == 4004);
    CHECK(o.stats.orderingTableNeed == d.stats.orderingTableNeed);
    CHECK(o.stats.rendererBytes() == 2 * (splash::kOtMin + 1) * 4 + 2 * 4004);
    CHECK(o.stats.bvhTriangleRefs == d.stats.bvhTriangleRefs);
    CHECK(splash::ramBudget(o.stats).used == o.stats.rendererBytes() + o.stats.visibleListBytes() +
                                                 std::max({o.stats.splashpackBytes, o.stats.vramFileBytes,
                                                           o.stats.spuFileBytes}));
}

// A script on an object with no mesh (the courtyard's Game Logic) ships as a
// game object with no triangles, so the engine still runs it.
void testScriptWithoutMesh() {
    namespace fs = std::filesystem;
    const fs::path project = fs::path(SPLASHEDIT_SOURCE_DIR) / "examples" / "courtyard";
    const fs::path dir = g_outDir / "meshless-script";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto exportBytes = [&](const splash::Scene& scene, splash::ExportResult& r) {
        r = splash::exportSplashpack(scene, project, dir / "scene.splashpack");
        std::ifstream in(dir / "scene.splashpack", std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    splash::Scene scene = splash::loadScene(project / "courtyard.scene");
    splash::ExportResult with;
    std::string a = exportBytes(scene, with);
    CHECK(with.ok());
    CHECK(a.find("scene-wide game logic") != std::string::npos);
    CHECK(a.find("Game Logic") != std::string::npos);
    std::erase_if(scene.objects, [](const splash::Object& o) { return o.name == "Game Logic"; });
    splash::ExportResult without;
    std::string b = exportBytes(scene, without);
    CHECK(without.ok());
    CHECK(b.find("scene-wide game logic") == std::string::npos);
    CHECK(with.stats.triangles == without.stats.triangles);
}

// LiveExport: waits for edits to settle, then exports the current revision.
void testLiveExport() {
    const std::filesystem::path project = std::filesystem::path(SPLASHEDIT_SOURCE_DIR) / "examples" / "courtyard";
    editor::Document d;
    CHECK(!d.load(project, "courtyard.scene"));
    editor::LiveExport live(0.3);
    live.update(d, 10.0);
    CHECK(!live.busy());
    live.update(d, 10.2);
    CHECK(!live.busy());
    live.update(d, 10.4);
    CHECK(live.busy());
    live.wait();
    CHECK(live.result() && live.result()->ok());
    CHECK(live.resultRevision() == d.revision());
    const size_t before = live.result()->stats.splashpackBytes;
    CHECK(d.duplicateObject({0}));
    live.update(d, 10.5);
    CHECK(!live.busy());
    live.update(d, 10.9);
    CHECK(live.busy());
    live.wait();
    CHECK(live.resultRevision() == d.revision());
    CHECK(live.result()->stats.splashpackBytes > before);
    live.update(d, 20.0);
    CHECK(!live.busy());  // nothing changed since the last export
}

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    if (*v) setenv(k, v, 1);
    else unsetenv(k);
#endif
}

void testPlay() {
    namespace fs = std::filesystem;
    const fs::path src = SPLASHEDIT_SOURCE_DIR;
    const fs::path project = src / "examples" / "courtyard";
    splash::Scene scene = splash::loadScene(project / "courtyard.scene");
    const fs::path dir = g_outDir / "play";
    fs::remove_all(dir);

    // The export lands under the names psxsplash's PCdrv loader opens.
    splash::ExportResult r = editor::exportForPlay(scene, project, dir);
    CHECK(r.ok());
    std::error_code ec;
    CHECK(fs::file_size(dir / "scene_0.splashpack", ec) == r.stats.splashpackBytes && r.stats.splashpackBytes > 0);
    CHECK(fs::file_size(dir / "scene_0.vram", ec) == r.stats.vramFileBytes && r.stats.vramFileBytes > 0);
    CHECK(fs::exists(dir / "scene_0.spu"));

    // A failed export leaves nothing behind to boot.
    splash::Scene bad = scene;
    splash::Object o = named("broken");
    o.mesh = splash::MeshComponent{};
    o.mesh->mesh = "no/such.mesh";  // and no materials: an export error
    bad.objects.push_back(o);
    splash::ExportResult b = editor::exportForPlay(bad, project, dir);
    CHECK(!b.ok());
    CHECK(!fs::exists(dir / "scene_0.splashpack") && !fs::exists(dir / "scene_0.vram") && !fs::exists(dir / "scene_0.spu"));

    // Settings: round trip (non-ASCII path), missing file, unknown keys.
    const fs::path cfg = g_outDir / "play" / "play.cfg";
    editor::PlayTools t;
    t.redux = fs::path(u8"/opt/rédux/pcsx-redux");
    t.psxsplash = "/x/psxsplash.ps-exe";
    CHECK(editor::savePlayTools(cfg, t));
    { std::ofstream(cfg, std::ios::app) << "colour=blue\nnot a pair\n"; }
    editor::PlayTools l = editor::loadPlayTools(cfg);
    CHECK(l.redux == t.redux && l.psxsplash == t.psxsplash && l.bios.empty());
    editor::PlayTools none = editor::loadPlayTools(dir / "absent.cfg");
    CHECK(none.redux.empty() && none.psxsplash.empty());

    // Environment fills only what the settings left empty.
    setEnv("SPLASHEDIT_PSXSPLASH", "/env/psxsplash.ps-exe");
    setEnv("SPLASHEDIT_REDUX", "/env/pcsx-redux");
    editor::PlayTools e = editor::withDefaults(l);
    CHECK(e.redux == t.redux && e.psxsplash == t.psxsplash);
    editor::PlayTools e2 = editor::withDefaults({});
    CHECK(e2.redux == fs::path("/env/pcsx-redux") && e2.psxsplash == fs::path("/env/psxsplash.ps-exe"));

    // A packaged editor carries redux/ and engine/ beside itself; the
    // environment still wins over them, and they win over PATH.
    fs::path bundle = dir / "bundle";
    fs::create_directories(bundle / "redux");
    fs::create_directories(bundle / "engine");
    std::ofstream(bundle / "redux" / "pcsx-redux") << "x";
    std::ofstream(bundle / "engine" / "psxsplash.ps-exe") << "x";
    editor::PlayTools envWins = editor::withDefaults({}, bundle);
    CHECK(envWins.redux == fs::path("/env/pcsx-redux") && envWins.psxsplash == fs::path("/env/psxsplash.ps-exe"));
    setEnv("SPLASHEDIT_PSXSPLASH", "");
    setEnv("SPLASHEDIT_REDUX", "");
    editor::PlayTools fromBundle = editor::withDefaults({}, bundle);
    CHECK(fromBundle.redux == bundle / "redux" / "pcsx-redux" &&
          fromBundle.psxsplash == bundle / "engine" / "psxsplash.ps-exe");
    CHECK(editor::missingTools(fromBundle).empty());
    editor::PlayTools empty = editor::withDefaults({}, dir / "nothing-here");
    CHECK(empty.psxsplash.empty());

    // What is missing, and the command once nothing is.
    CHECK(editor::missingTools({}).size() == 2);
    editor::PlayTools real;
    real.redux = dir / "play.cfg";  // any existing file stands in for the programs here
    real.psxsplash = dir / "play.cfg";
    CHECK(editor::missingTools(real).empty());
    real.bios = dir / "absent.bin";
    CHECK(editor::missingTools(real).size() == 1);
    real.bios.clear();
    std::vector<std::string> cmd = editor::reduxCommand(t, "/b");
    std::vector<std::string> want = {(const char*)u8"/opt/rédux/pcsx-redux", "-run", "-fastboot", "-no-ui",
                                     "-shmdisplay", "-loadexe", "/x/psxsplash.ps-exe", "-pcdrv", "-pcdrvbase", "/b"};
    CHECK(cmd == want);
    t.bios = "/x/openbios.bin";
    cmd = editor::reduxCommand(t, "/b");
    CHECK(cmd.size() == want.size() + 2 && cmd[5] == "-bios" && cmd[6] == "/x/openbios.bin");
}

// File menu: a new project on disk, Save As inside it, and the recent list.
void testProject() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = g_outDir / "project";
    fs::remove_all(dir, ec);
    fs::path proj = dir / "My Game";

    editor::NewProject made = editor::createProject(proj);
    CHECK(made.error.empty());
    CHECK(made.scene == fs::path("My Game.scene"));
    CHECK(fs::is_directory(proj / "meshes") && fs::is_directory(proj / "textures") && fs::is_directory(proj / "scripts"));
    CHECK(fs::is_regular_file(proj / made.scene));
    CHECK(editor::firstScene(proj) == made.scene);

    // The new scene loads, empty, and is not dirty.
    editor::Document doc;
    CHECK(!doc.load(proj, made.scene));
    CHECK(doc.objectCount() == 0 && !doc.dirty());

    // A folder that already holds a scene is never overwritten.
    CHECK(doc.insertObject({0}, named("Crate")));
    CHECK(!doc.save());
    editor::NewProject again = editor::createProject(proj);
    CHECK(!again.error.empty() && again.scene.empty());
    editor::Document reread;
    CHECK(!reread.load(proj, made.scene) && reread.objectCount() == 1);

    // Save As: inside the project only, with .scene added when missing.
    CHECK(editor::sceneInProject(proj, proj / "level2") == fs::path("level2.scene"));
    CHECK(editor::sceneInProject(proj, proj / "levels" / "a.scene") == fs::path("levels") / "a.scene");
    CHECK(!editor::sceneInProject(proj, dir / "outside.scene"));
    CHECK(!editor::sceneInProject(proj, proj / ".." / "escape.scene"));
    CHECK(!editor::sceneInProject({}, proj / "a.scene"));
    CHECK(!doc.saveAs("level2.scene"));
    CHECK(doc.sceneStem() == "level2" && doc.scenePath() == proj / "level2.scene" && !doc.dirty());
    CHECK(fs::is_regular_file(proj / "level2.scene"));
    editor::Document level2;
    CHECK(!level2.load(proj, "level2.scene") && level2.objectCount() == 1);
    // A failed Save As keeps the document on its old file.
    fs::create_directories(proj / "blocked.scene", ec);
    CHECK(doc.saveAs("blocked.scene").has_value());
    CHECK(doc.sceneStem() == "level2");

    // Recent scenes: newest first, no duplicates, capped, persisted.
    fs::path cfg = dir / "recent.cfg";
    {
        editor::RecentScenes r(cfg);
        r.load();
        CHECK(r.items().empty());
        for (int i = 0; i < 10; ++i) r.add(proj / ("s" + std::to_string(i) + ".scene"));
        CHECK(r.items().size() == editor::RecentScenes::kMax);
        CHECK(r.items().front() == proj / "s9.scene");
        r.add(proj / "s5.scene");
        CHECK(r.items().front() == proj / "s5.scene" && r.items().size() == editor::RecentScenes::kMax);
        CHECK(std::count(r.items().begin(), r.items().end(), proj / "s5.scene") == 1);
        CHECK(r.items().back() == proj / "s2.scene");
        r.remove(proj / "s9.scene");
        CHECK(r.items().size() == editor::RecentScenes::kMax - 1);
    }
    editor::RecentScenes back(cfg);
    back.load();
    CHECK(back.items().size() == editor::RecentScenes::kMax - 1 && back.items().front() == proj / "s5.scene");
    CHECK(std::find(back.items().begin(), back.items().end(), proj / "s9.scene") == back.items().end());
}

}  // namespace

int main(int argc, char** argv) {
    std::error_code ec;
    std::filesystem::path exe = argc > 0 ? std::filesystem::absolute(argv[0], ec) : std::filesystem::path();
    g_outDir = (exe.has_parent_path() ? exe.parent_path() : std::filesystem::current_path()) / "editor_tests_out";
    testApplyUndoRedo();
    testMerge();
    testSettings();
    testRenderPeak();
    testDirtyAndSave();
    testStructure();
    testPick();
    testGizmoMath();
    testScaleClamp();
    testGizmoUndo();
    testCatalog();
    testExportStats();
    testScriptWithoutMesh();
    testLiveExport();
    testPlay();
    testProject();
    if (g_failures) {
        std::fprintf(stderr, "editor_tests: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("editor_tests: all %d checks passed\n", g_checks);
    return 0;
}
