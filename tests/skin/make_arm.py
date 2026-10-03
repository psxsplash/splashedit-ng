# Writes the test arm: two 0.5 m boxes stacked on Y, the lower one on joint
# "shoulder" (at the origin), the upper one on "elbow" (y = 1, child of the
# shoulder), and two clips. Regenerate with: python3 make_arm.py
import json, math, os

here = os.path.dirname(os.path.abspath(__file__))


def box(y0, y1, joint, pos, nrm, uv, vj, vw, tris):
    h = 0.25
    faces = [((1, 0, 0), [(h, y0, -h), (h, y1, -h), (h, y1, h), (h, y0, h)]),
             ((-1, 0, 0), [(-h, y0, h), (-h, y1, h), (-h, y1, -h), (-h, y0, -h)]),
             ((0, 1, 0), [(-h, y1, -h), (-h, y1, h), (h, y1, h), (h, y1, -h)]),
             ((0, -1, 0), [(-h, y0, h), (-h, y0, -h), (h, y0, -h), (h, y0, h)]),
             ((0, 0, 1), [(h, y0, h), (h, y1, h), (-h, y1, h), (-h, y0, h)]),
             ((0, 0, -1), [(-h, y0, -h), (-h, y1, -h), (h, y1, -h), (h, y0, -h)])]
    for n, quad in faces:
        b = len(pos) // 3
        for i, p in enumerate(quad):
            pos.extend(p)
            nrm.extend(n)
            uv.extend([(0, 0), (0, 1), (1, 1), (1, 0)][i])
            vj.extend([joint, 0, 0, 0])
            vw.extend([1, 0, 0, 0])
        # Unity winding: clockwise seen from outside.
        tris.extend([b, b + 1, b + 2, b, b + 2, b + 3])


pos, nrm, uv, vj, vw, tris = [], [], [], [], [], []
box(0, 1, 0, pos, nrm, uv, vj, vw, tris)
box(1, 2, 1, pos, nrm, uv, vj, vw, tris)
mesh = {"format": "splashedit-ng/mesh", "version": 1, "positions": pos, "normals": nrm, "uv": uv,
        "submeshes": [tris],
        "skin": {"joints": [{"name": "shoulder", "parent": -1, "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
                            {"name": "elbow", "parent": 0, "position": [0, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}],
                 "vertexJoints": vj, "vertexWeights": vw}}
json.dump(mesh, open(os.path.join(here, 'arm.mesh'), 'w'))


def zrot(deg):
    a = math.radians(deg) / 2
    return [0, 0, round(math.sin(a), 7), round(math.cos(a), 7)]


bend = {"format": "splashedit-ng/anim", "version": 1, "name": "bend", "length": 1.0, "loop": True,
        "channels": [{"joint": "elbow", "property": "rotation", "interpolation": "linear",
                      "times": [0, 0.5, 1.0], "values": zrot(0) + zrot(90) + zrot(0)}]}
raise_ = {"format": "splashedit-ng/anim", "version": 1, "name": "raise", "length": 0.5, "loop": False,
          "channels": [{"joint": "shoulder", "property": "rotation", "interpolation": "linear",
                        "times": [0, 0.5], "values": zrot(0) + zrot(-45)}]}
json.dump(bend, open(os.path.join(here, 'bend.anim'), 'w'), indent=1)
json.dump(raise_, open(os.path.join(here, 'raise.anim'), 'w'), indent=1)
