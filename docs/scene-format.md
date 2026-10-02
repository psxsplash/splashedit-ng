# Scene and asset files

A project is one folder. Paths inside project files are relative to the project root and use `/`.
Everything is plain text except images and audio, so scenes diff and merge in git.

## Scene file (`*.scene`)

JSON, UTF-8, two-space indent, keys in the order given here. Unknown keys are kept on load and
written back (in their original position) so newer files survive older editors.

```json
{
  "format": "splashedit-ng/scene",
  "version": 1,
  "settings": {
    "gteScaling": 100,
    "sceneType": "exterior",
    "fog": { "enabled": false, "color": [0.5, 0.5, 0.6], "density": 5 },
    "networkId": "",
    "script": null
  },
  "objects": [
    {
      "name": "Crate",
      "active": true,
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "components": [
        { "type": "mesh", "mesh": "assets/crate.mesh", "materials": [ { "texture": "assets/crate.png", "color": [1, 1, 1, 1] } ],
          "bitDepth": 8, "vertexColors": "baked", "flatColor": [128, 128, 128], "smoothNormals": true, "uvOffsetMaterial": 0 },
        { "type": "collider", "kind": "static" },
        { "type": "script", "lua": "scripts/crate.lua" }
      ],
      "children": []
    }
  ]
}
```

- `rotation` is a quaternion `[x, y, z, w]`; the editor shows Euler angles but stores this, so a
  save/load cycle is exact. Coordinates are Y-up, left-handed (the same space Unity uses), metres
  divided by `gteScaling` into GTE units at export.
- `objects` is a tree (`children`). **Canonical order** for everything the splashpack indexes by
  position (game objects, name table, mesh data, Lua files, texture packing ties) is depth-first
  pre-order over this tree in file order. The editor never reorders on save, so the order is the
  one the user sees in the hierarchy panel.
- Floats are written with the shortest representation that round-trips a 32-bit float.

Components so far (more are added feature by feature, matching the splashpack sections):

| type | fields |
|---|---|
| `mesh` | `mesh` (path to a `.mesh`), `materials` (one per submesh: `texture` path or null, `color` RGBA 0..1), `bitDepth` 4/8/16, `vertexColors` `baked`/`flat`/`mesh`, `flatColor` RGB 0..255, `smoothNormals`, `uvOffsetMaterial` |
| `collider` | `kind`: `none`, `static`, `dynamic`; `platform` bool |
| `script` | `lua`: path to a `.lua` file |
| `light` | `kind` `directional`/`point`/`spot`, `color` RGB 0..1, `intensity`, `range`, `spotAngle`, `innerSpotAngle` |

## Mesh file (`*.mesh`)

JSON, the imported form of a model: what the exporter consumes after any importer (glTF, OBJ, FBX)
has run. Arrays are flat.

```json
{
  "format": "splashedit-ng/mesh",
  "version": 1,
  "positions": [x0, y0, z0, x1, ...],
  "normals": [ ... ],
  "uv": [u0, v0, ...],
  "colors": [r0, g0, b0, a0, ...],
  "submeshes": [ [i0, i1, i2, ...], ... ]
}
```

`normals`, `uv` and `colors` may be absent. Bounds are computed from `positions`.

## Images

PNG, 8-bit RGBA. A channel value `c` is read as `c / 255` in 32-bit float. Sizes above 256 are
downscaled at import (the PS1 texture page limit).
