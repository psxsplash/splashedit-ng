# Scene and asset files

A project is one folder. Paths inside project files are relative to the project root and use `/`.
Everything is plain text except images and audio, so scenes diff and merge in git.

## Scene file (`*.scene`)

JSON, UTF-8, two-space indent, keys in the order given here. Unknown keys, and components of an
unknown type, are kept on load and written back after the known ones, so a file from a newer build
survives a save in an older one.

`version` changes only when an older build would read a file wrong. Files with no version, or a
version higher than the build's, fail to load. Additions an older build can skip keep the
version as it is.

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
| `light` | `kind` `directional`/`point`/`spot`, `color` RGB 0..1, `intensity`, `range`, `spotAngle`, `innerSpotAngle`, `enabled` true |
| `player` | `playerHeight` 1.8 (eye height above the feet), `playerRadius` 0.5, `moveSpeed` 3, `sprintSpeed` 8 (units/s), nav bake fields (below), `jumpHeight` 2, `gravity` 20 |
| `navigation` | nav bake without a player: `agentHeight` 1.8, `agentRadius` 0.5, nav bake fields (below), `spawnAnchor` object name or null (null = this object) |
| `trigger` | `size` [1, 1, 1] box size before the object's transform, `lua` script path or null. The written box is the world AABB of the transformed box; the script gets `onTriggerEnter(index)` and `onTriggerExit(index)` |
| `interactable` | `radius` 2, `button` 14 (pad bit, 14 = Cross), `repeatable` true, `cooldownFrames` 30, `showPrompt` false, `promptCanvas` "" (15 bytes kept), `lineOfSight` false |
| `audio` | `clip` WAV path or null, `clipName` "" (the name Lua plays it by), `sampleRate` 22050, `loop` false, `defaultVolume` 100, `trimLeadingSilence` false. Channels are averaged to mono, resampled to `sampleRate` and encoded to SPU-ADPCM |

Nav bake fields, shared by `player` and `navigation` (defaults in brackets): `maxStepHeight` [0.35],
`walkableSlopeAngle` degrees [46], `navCellSize` [0.05], `navCellHeight` [0.025], `navMinRegionArea`
voxels [8], `navMergeRegionArea` voxels [20], `navMaxSimplifyError` [1.3], `navMaxEdgeLength` [12],
`navPartitionMethod` `watershed`/`monotone`/`layer` [`watershed`], `navDetailSampleDist` multiple of
the cell size [6], `navDetailMaxError` [0.025], `navMaxPlaneError` [0.15, editor warning only, does
not change the export].

The exporter uses the first active object (canonical order) carrying each of these. The player
start is the point straight below the player's position on the nearest upward-facing triangle of
any active mesh within 100 units, raised by `playerHeight` (no hit: the position raised by
`playerHeight`); its rotation is the player's world rotation. Without a player the start is the
`navigation` spawn point (or the origin) with no rotation, and height/radius come from `navigation`
(or 1.8/0.5). The nav mesh is baked from the meshes of objects with a `static` collider, with the
agent size of the player (or 1.8/0.5); if a `navigation` component exists, all its values win over
the player's.

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

`normals`, `uv` and `colors` may be absent; when present they have one entry per position. Each array's
length must be a multiple of its tuple size. Bounds are computed from `positions`.

## Images

PNG, 8-bit RGBA. A channel value `c` is read as `c / 255` in 32-bit float. Sizes above 256 are
downscaled at import (the PS1 texture page limit).
