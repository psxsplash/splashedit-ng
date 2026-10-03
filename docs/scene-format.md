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
| `skin` | `clips` (paths to `.anim` files, 1..16), `fps` 1..30 [15]. Needs a `mesh` component whose `.mesh` has a `skin`. Lua plays a clip with `SkinnedAnim.Play(objectName, clipName)` |
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

### UI: `fonts` and `canvases`

Two optional top-level arrays after `objects`.

```json
"fonts": [
  { "name": "title", "source": "fonts/Inter-SemiBold.ttf", "size": 16 },
  { "name": "pixel", "bitmap": "fonts/pixel.png", "glyphWidth": 8, "glyphHeight": 12 }
],
"canvases": [
  { "name": "hud", "visible": true, "sortOrder": 0, "elements": [
    { "type": "box", "name": "bar", "visible": true, "rect": [0, -20, 0, 20],
      "anchorMin": [0, 1], "anchorMax": [1, 1], "color": [0.1, 0.3, 0.8] },
    { "type": "text", "name": "score", "visible": true, "rect": [8, 8, 120, 16],
      "anchorMin": [0, 0], "anchorMax": [0, 0], "color": [1, 1, 1], "text": "Score 0", "font": "title" }
  ] }
]
```

A font is either `source`, a TTF/OTF rasterised at `size` pixels per em (4..64), or `bitmap`, a PNG
256 pixels wide of `glyphWidth` x `glyphHeight` cells from 0x20 in ASCII order (ink = alpha above
0.5). A bitmap font may give `advances`, 96 widths for 0x20..0x7F; without them each character
advances by its ink width plus one. At most 3 fonts; names must be unique.

Elements are drawn in array order. `type` is `box`, `text`, `progress`, `line` or `image`. Colours
are 0..1. Layout is in screen pixels: on each axis with `anchorMin` equal to `anchorMax`, `rect`
x (or y) is the offset of the top-left corner from that fraction of the screen and w (or h) the
size; with them apart the element stretches, x/y are the left/top insets and the right/bottom edge
is the `anchorMax` point plus x + w (or y + h). Per type: `text` has `text` (ASCII; at most 63 bytes
are kept) and `font` (a font name, `null` for the system font); `progress` has `background` and
`value` (0..100); `line` has `from` and `to` in screen pixels instead of `rect` and anchors;
`image` has `texture`, `bitDepth` (4, 8, 16) and `cutout` [true].

### Cutscenes: `cutscenes`

An optional top-level array, played from Lua with `Cutscene.Play(name)`.

```json
"cutscenes": [
  { "name": "intro", "durationFrames": 120,
    "tracks": [
      { "type": "objectPosition", "target": "Crate",
        "keyframes": [ { "frame": 0, "value": [-1.5, 0, 4], "interp": "linear" },
                       { "frame": 120, "value": [1.5, 0, 4], "interp": "easeOut" } ] },
      { "type": "uiProgress", "target": "hud/bar", "keyframes": [ { "frame": 0, "value": [0, 0, 0] } ] }
    ],
    "audioEvents": [ { "frame": 30, "clip": "door", "volume": 100, "pan": 64 } ] }
]
```

Track types and the units of `value`: `cameraPosition`, `objectPosition` (scene units; at most 8 PSX
units, 8 x gteScaling, from the origin), `cameraRotation`, `objectRotation` (Euler degrees, as the
object transform), `objectActive`, `uiCanvasVisible`, `uiElementVisible`, `rumbleSmall` (x above 0.5
= on), `uiProgress` (0..100), `uiPosition` (x, y in pixels, the element's `rect` offset), `uiColor`
(0..1), `cameraH` (projection distance, 1..1024), `rumbleLarge` (0..255), `objectUVOffset` (texels,
0..255). Object tracks target an object with a mesh by name, `uiCanvasVisible` a canvas name, other
UI tracks `canvas/element`; camera and rumble tracks have no target. `interp` is `linear`, `step`,
`easeIn`, `easeOut` or `easeInOut`. Frames are 0..8191 and keys may be in any order. Audio events
name an audio component's `clipName`; `volume` is 0..128 [100], `pan` 0..127 [64].

At most 16 cutscenes, 8 tracks each, 64 keys per track and 64 audio events. A target or clip that
does not exist fails the export.

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

A skinned mesh adds `skin`:

```json
"skin": {
  "joints": [ { "name": "hip", "parent": -1, "position": [0, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }, ... ],
  "inverseBind": [ 12 floats per joint, 3x4 row-major ],
  "vertexJoints": [ 4 joint indices per vertex ],
  "vertexWeights": [ 4 weights per vertex ]
}
```

Joint transforms are local to the parent (`-1` = mesh space) and give the rest pose; a parent comes
before its children. `inverseBind` maps mesh space to each joint's space at bind time and may be
left out, in which case the inverse of the rest pose is used. The PS1 skins rigidly: each vertex
follows the joint with its largest weight. psxsplash draws at most 64 joints.

## Animation clip (`*.anim`)

```json
{
  "format": "splashedit-ng/anim",
  "version": 1,
  "name": "walk",
  "length": 1.0,
  "loop": true,
  "channels": [
    { "joint": "knee_l", "property": "rotation", "interpolation": "linear",
      "times": [0, 0.5, 1.0], "values": [x, y, z, w, ...] }
  ]
}
```

`property` is `position`, `scale` (3 values per key) or `rotation` (quaternion, 4 values);
`interpolation` is `linear` (rotations slerp) or `step`. Joints without a channel hold their rest
pose. `name` is what Lua plays and is 1..24 characters; names are unique per object.

Export samples each clip at the skin's `fps`. A looping clip stores `round(length * fps)` frames over
`[0, length)`: the engine wraps from the last frame to the first and blends between them, so the end
pose is not stored a second time (2.4.0 stored it, which held the loop seam for one frame). A one-shot
clip stores the frames at `i / fps` up to and including the end pose.

## Images

PNG, 8-bit RGBA. A channel value `c` is read as `c / 255` in 32-bit float. Sizes above 256 are
downscaled at import (the PS1 texture page limit).
