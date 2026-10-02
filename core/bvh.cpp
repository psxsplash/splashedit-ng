#include "bvh.hh"

#include <deque>
#include <memory>

#include "netsort.hh"

namespace splash {

namespace {

constexpr int kMaxTrisPerLeaf = 64;
constexpr int kMaxDepth = 16;
constexpr int kMinTrisToSplit = 8;
constexpr int kBins = 8;
constexpr float kTraversalCost = 1.0f;
constexpr float kIntersectCost = 1.0f;

struct Tri {
    TriangleRef ref;
    Bounds bounds;
    Vec3 centroid;
};

struct Node {
    Bounds bounds;
    std::unique_ptr<Node> left, right;
    std::vector<TriangleRef> tris;  // leaves only
    size_t trisCapacity = 0;        // List<T> capacity, feeds the sort depth limit
    int index = -1;
    bool leaf() const { return !left && !right; }
};

// The original's cost arithmetic is plain C# float code, but Unity's Mono JIT
// evaluates it with double intermediates: split costs tie exactly on symmetric
// meshes, and only double evaluation reproduces which axis wins.
double halfSurfaceArea(const Bounds& b) {
    Vec3 s = b.size();
    return double(s.x) * s.y + double(s.y) * s.z + double(s.z) * s.x;
}

float axisOf(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

void makeLeaf(Node& n, const std::vector<Tri>& tris) {
    for (const Tri& t : tris) n.tris.push_back(t.ref);
    // Select(...).ToList() enumerates into a List one Add at a time.
    n.trisCapacity = netsort::listCapacityAfterAdds(n.tris.size());
}

std::unique_ptr<Node> build(const std::vector<Tri>& tris, int depth) {
    if (tris.empty()) return nullptr;
    auto node = std::make_unique<Node>();
    node->bounds = tris[0].bounds;
    for (const Tri& t : tris) node->bounds.encapsulate(t.bounds);

    int count = int(tris.size());
    if (count <= kMaxTrisPerLeaf || depth >= kMaxDepth || count < kMinTrisToSplit) {
        makeLeaf(*node, tris);
        return node;
    }
    double parentArea = halfSurfaceArea(node->bounds);
    if (parentArea <= 0.f) {
        makeLeaf(*node, tris);
        return node;
    }

    Bounds cb(tris[0].centroid, {});
    for (size_t i = 1; i < tris.size(); i++) cb.encapsulate(tris[i].centroid);

    double bestCost = std::numeric_limits<float>::max();
    int bestAxis = -1, bestSplit = -1;
    for (int axis = 0; axis < 3; axis++) {
        float cMin = axisOf(cb.min(), axis), cMax = axisOf(cb.max(), axis);
        float range = cMax - cMin;
        if (range <= 1e-6f) continue;
        int binCount[kBins] = {};
        Bounds binBounds[kBins];
        bool binInit[kBins] = {};
        float invRange = float(kBins) / range;
        for (const Tri& t : tris) {
            float c = axisOf(t.centroid, axis);
            int bin = clampv(int((c - cMin) * invRange), 0, kBins - 1);
            binCount[bin]++;
            if (!binInit[bin]) {
                binBounds[bin] = t.bounds;
                binInit[bin] = true;
            } else {
                binBounds[bin].encapsulate(t.bounds);
            }
        }
        int leftCount[kBins - 1], rightCount[kBins - 1];
        float leftArea[kBins - 1], rightArea[kBins - 1];
        {
            int run = 0;
            Bounds rb;
            bool init = false;
            for (int s = 0; s < kBins - 1; s++) {
                run += binCount[s];
                if (binCount[s] > 0) {
                    if (!init) {
                        rb = binBounds[s];
                        init = true;
                    } else {
                        rb.encapsulate(binBounds[s]);
                    }
                }
                leftCount[s] = run;
                leftArea[s] = init ? float(halfSurfaceArea(rb)) : 0.f;
            }
        }
        {
            int run = 0;
            Bounds rb;
            bool init = false;
            for (int s = kBins - 2; s >= 0; s--) {
                run += binCount[s + 1];
                if (binCount[s + 1] > 0) {
                    if (!init) {
                        rb = binBounds[s + 1];
                        init = true;
                    } else {
                        rb.encapsulate(binBounds[s + 1]);
                    }
                }
                rightCount[s] = run;
                rightArea[s] = init ? float(halfSurfaceArea(rb)) : 0.f;
            }
        }
        for (int s = 0; s < kBins - 1; s++) {
            if (leftCount[s] == 0 || rightCount[s] == 0) continue;
            double cost = double(kTraversalCost) + (double(leftCount[s]) * leftArea[s] + double(rightCount[s]) * rightArea[s]) *
                                              kIntersectCost / parentArea;
            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplit = s;
            }
        }
    }

    double leafCost = double(count) * kIntersectCost;
    if (bestAxis < 0 || bestCost >= leafCost) {
        makeLeaf(*node, tris);
        return node;
    }

    float splitMin = axisOf(cb.min(), bestAxis), splitMax = axisOf(cb.max(), bestAxis);
    float splitInv = float(kBins) / (splitMax - splitMin);
    std::vector<Tri> l, r;
    for (const Tri& t : tris) {
        float c = axisOf(t.centroid, bestAxis);
        int bin = clampv(int((c - splitMin) * splitInv), 0, kBins - 1);
        (bin <= bestSplit ? l : r).push_back(t);
    }
    if (l.empty() || r.empty()) {
        makeLeaf(*node, tris);
        return node;
    }
    node->left = build(l, depth + 1);
    node->right = build(r, depth + 1);
    return node;
}

}  // namespace

Bvh buildBvh(const std::vector<BvhInputObject>& objects) {
    std::vector<Tri> tris;
    for (size_t oi = 0; oi < objects.size(); oi++) {
        const BvhInputObject& o = objects[oi];
        if (!o.include) continue;
        const auto& v = *o.vertices;
        for (size_t i = 0; i + 2 < o.triangles.size(); i += 3) {
            Vec3 v0 = o.localToWorld.point(v[size_t(o.triangles[i])]);
            Vec3 v1 = o.localToWorld.point(v[size_t(o.triangles[i + 1])]);
            Vec3 v2 = o.localToWorld.point(v[size_t(o.triangles[i + 2])]);
            Bounds b(v0, {});
            b.encapsulate(v1);
            b.encapsulate(v2);
            tris.push_back({{uint16_t(oi), uint16_t(i / 3)}, b, (v0 + v1 + v2) / 3.f});
        }
    }
    Bvh out;
    if (tris.empty()) return out;
    std::unique_ptr<Node> root = build(tris, 0);

    std::vector<Node*> order;
    std::deque<Node*> queue{root.get()};
    while (!queue.empty()) {
        Node* n = queue.front();
        queue.pop_front();
        n->index = int(order.size());
        order.push_back(n);
        if (n->left) queue.push_back(n->left.get());
        if (n->right) queue.push_back(n->right.get());
    }
    for (Node* n : order) {
        BvhNode bn;
        bn.bounds = n->bounds;
        if (n->left) bn.left = n->left->index;
        if (n->right) bn.right = n->right->index;
        if (n->leaf() && !n->tris.empty()) {
            netsort::listSort(
                n->tris,
                [](const TriangleRef& a, const TriangleRef& b) { return int(a.objectIndex) - int(b.objectIndex); },
                n->trisCapacity);
            bn.firstTriangle = int(out.refs.size());
            bn.triangleCount = int(n->tris.size());
            out.refs.insert(out.refs.end(), n->tris.begin(), n->tris.end());
        }
        out.nodes.push_back(bn);
    }
    return out;
}

}  // namespace splash
