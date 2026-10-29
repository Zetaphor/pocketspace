# Model lighting

Lighting is opt-in for each shared model asset. Existing scripts keep the flat,
base-color rendering path. Both paths honor glTF single-/double-sided materials. Load a model with `pbr: true` to use its metallic/roughness
materials and surface details:

```js
const car = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true});
setModelLighting(car, {
  direction: [-.4, .8, .5],
  color: [1, .95, .85],
  intensity: 3,
  ambient: .15,
  exposure: 1
});
const object = createObject(car, 0, 0, 0, .3);
enableObjectInteraction(object, {grabButton: 'both'});
```

The exact path is cached. Repeating `loadModel` without `pbr` reuses its existing
choice; explicitly requesting a different choice for that path throws. Lighting
settings apply to every object using that asset. Different assets can have
different lighting. Lighting follows the scene anchor, rather than the user's
head or a grabbed object.

## Lighting controls

`setModelLighting(assetId, options)` updates only supplied fields. The asset must
have been loaded with `pbr: true`. Invalid values throw before any lighting
changes are published. Getter side effects remain ordinary script actions.

| Option | Meaning | Default and range |
| --- | --- | --- |
| `direction` | Direction **toward** the directional light; three numbers `[x, y, z]`, with X right, Y up, Z toward the viewer in the scene's model axes | `[-.4, .8, .5]`; components −64 through 64; length at least .000001 |
| `color` | Linear RGB light color | `[1, 1, 1]`; three components from 0 through 64 |
| `intensity` | Directional light multiplier | `3`; 0 through 64 |
| `ambient` | Constant diffuse ambient multiplier | `.15`; 0 through 16 |
| `exposure` | Exposure before the exponential tone curve | `1`; .001 through 64 |

Direction is normalized by the shader. All numeric values must be finite.
Lighting is not a new scene object: this first implementation provides one
unshadowed directional light and constant diffuse ambient light per asset.
Emissive materials do not illuminate other objects.

## Imported materials and surface geometry

The lit path uses glTF's metallic/roughness factors and embedded base-color,
metallic/roughness, normal, occlusion and emissive textures. Roughness uses the
texture's green channel; metalness uses blue. Base-color and emissive textures
are sampled as sRGB and decoded before filtering/lighting; data textures remain
linear. Sampler wrapping/filtering and independent `KHR_texture_transform`
transforms are applied. This implementation supports `TEXCOORD_0`; selecting a
different texture-coordinate set is rejected. A zero normal-map UV scale is
rejected.

Supplied normals and tangent handedness are retained. Missing normals produce
flat triangle normals. Missing tangents use MikkTSpace, with the normal map's UV
transform. Surface frames account for node transforms, including nonuniform and
negative scaling, and imported normal/tangent morph deltas. Animated models use
independent CPU-deformed surface frames, positions, picking and bounds.

Shading uses a GGX metallic/roughness BRDF with Schlick Fresnel, correlated Smith
visibility, and dielectric reflectance of .04. Normal maps use glTF's OpenGL
orientation. Occlusion affects ambient diffuse light. Lit colors pass through
`1 - exp(-color * exposure)` and linear-to-sRGB output encoding. Alpha masking and
blending keep the existing compositor conventions. Single-sided materials cull
back faces in both the lit and unlit paths; two-sided materials reverse back-face normals. `KHR_materials_unlit` bypasses
lighting and tone mapping, and `KHR_materials_emissive_strength` scales emission.

## Current limits and performance

This is a direct-light PBR implementation, not a complete glTF material renderer.
There are no shadows, image-based reflections/environment maps, punctual-light
imports, bloom, transmission, clearcoat, physical sheen, or other advanced
material extensions. Unsupported optional material extensions use the core
metallic/roughness fallback. Metals can look dark away from their direct-light
highlight because there is no reflected environment.

Lit assets currently use full-detail expanded geometry to preserve normal and
UV seams. The unlit position/UV indexer, automatic LOD, ordered instancing, rigid
part batching and GPU skinning do not run on lit assets. Loading options for
those policies remain stored, but do not enable those paths for PBR. Additional
surface buffers and material textures are included in the engine's GPU allocation
estimate. Choose lit assets deliberately for dense scenes; no PBR throughput gain
is implied.

Scene-container snapshots preserve the PBR choice and lighting settings. Restore
rejects a conflicting cached asset instead of changing existing objects' shared
lighting. Older snapshots restore unlit models. See [scene persistence](scene-persistence.md).

See [pbr.js](../scripts/pbr.js) for a concise interactive example. The separate
[Khronos material test scene](../tests/scenes/pbr-materials.js) covers the lighter
diagnostic models without capturing controller input. The two high-triangle
metal/roughness grids stay in offscreen regression checks.
