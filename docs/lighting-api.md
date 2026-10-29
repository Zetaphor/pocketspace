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
  ambient: 0,
  environment: 'assets/environments/studio.sfenv',
  environmentIntensity: 1,
  environmentRotation: 0,
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
| `environment` | Path to a baked `.sfenv` lighting environment; `null` disables it | Disabled; nonempty string up to 4096 bytes, or `null` |
| `environmentIntensity` | Diffuse and specular environment multiplier | `1`; 0 through 64 |
| `environmentRotation` | Rotate the environment around scene +Y, in radians | `0`; −10000 through 10000 |

Direction is normalized by the shader. All numeric values must be finite.
Lighting is not a new scene object: it provides one unshadowed directional
light, constant diffuse ambient light and an optional baked environment per asset.
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
transform and glTF handedness conversion. Imported glTF tangent signs are retained. Surface frames account for node transforms, including nonuniform and
negative scaling, and imported normal/tangent morph deltas. Animated models use
independent CPU-deformed surface frames, positions, picking and bounds.

Shading uses a GGX metallic/roughness BRDF with Schlick Fresnel, correlated Smith
visibility, and dielectric reflectance of .04. Normal maps use glTF's OpenGL
orientation. Occlusion affects ambient diffuse light. Lit colors pass through
`1 - exp(-color * exposure)` and linear-to-sRGB output encoding. Alpha masking and
blending keep the existing compositor conventions. Single-sided materials cull
back faces in both the lit and unlit paths; two-sided materials reverse back-face normals. `KHR_materials_unlit` bypasses
lighting and tone mapping, and `KHR_materials_emissive_strength` scales emission.

## Environment lighting and reflections

The supplied studio bake has a clear horizon and bright windows, so metals
reflect a recognizable environment even away from the directional-light
highlight. Diffuse surfaces receive low-frequency environment illumination too.
It is not a reflection of nearby engine objects, the headset camera, or the real
room. No background skybox is drawn.

The environment stays aligned with the scene anchor. Rotating an object changes
its reflections; moving your head changes the viewing angle; `environmentRotation`
rotates the lighting around +Y. Intensity zero removes environment lighting while
retaining its selection; `environment: null` releases that asset's reference.
Omitted fields keep their current values. `KHR_materials_unlit` ignores all lighting.
Occlusion textures attenuate both diffuse and specular environment terms.

The exact file path is shared between model assets and live reload candidates.
Loading validates the complete bounded file before changing an asset's lighting.
A missing, malformed or unsupported file throws without publishing partial
lighting changes. As with model paths, cached files are immutable while they are
referenced; choose a new path to load different content. Scene snapshots store
the path and controls, not the environment pixels. Keep the file available when
restoring on another device. Older v1–v6 snapshots restore without an environment.

At runtime the engine uploads a prefiltered HDR cubemap and BRDF lookup once per
shared environment. Shading uses a roughness-selected cube sample, one BRDF
lookup and nine diffuse spherical-harmonic coefficients. Baking and convolution
happen offline. Environment textures are included once in the GPU allocation
estimate, including a conservative allowance for RGB16F padding. The supplied
bake occupies about 1.06 MiB of estimated GPU storage regardless of how many
models share it. Actual renderer/driver allocation may differ.

### Baking a custom environment

Bake on your desktop before deploying to the headset. The offline tools need
Python; the engine does not. From the repository root, install into a virtual
environment:

```sh
python3 -m venv /tmp/sf-environment-venv
/tmp/sf-environment-venv/bin/pip install -r tools/requirements-environment.txt
```

Start with a full-sphere equirectangular panorama (usually 2:1), such as a `.hdr`
or `.exr` lighting image. Convert it, then precompute the reflections and diffuse
lighting:

```sh
/tmp/sf-environment-venv/bin/python tools/convert-environment.py /path/to/panorama.hdr /tmp/lighting.npy --color-space linear
/tmp/sf-environment-venv/bin/python tools/generate-environment.py assets/environments/custom.sfenv --input /tmp/lighting.npy
```

Use `--color-space linear` for linear HDR radiance, or `--color-space srgb` for
an ordinary sRGB PNG/JPEG panorama. The converter normalizes integer image data
through OpenImageIO and decodes sRGB when requested. Alpha is discarded. It does
not tone-map, rescale exposure, reproject cube crosses or convert color primaries:
convert ACEScg/wide-gamut sources to linear Rec.709/sRGB primaries first. An LDR
image works but cannot recover bright light sources clipped in the original.
Keep upstream image credits and redistribution licenses with custom assets.

The intermediate `.npy` contains finite, nonnegative H×W×3 linear RGB values,
maximum 65504. Existing arrays can be passed directly to the baker; only NumPy is
needed in that case. The image's top is +Y; center longitude is +X and the right
quarter is +Z. Neither tool flips or rotates the panorama. Raw HDR/EXR/KTX files
are not accepted by the runtime loader.

The baker accepts `--size` and `--lut-size` of 16, 32, 64, 128 or 256, and
`--samples` from 64 to 4096. Defaults are 128/128 with 256 samples. Larger cubes
retain sharper reflections and cost more storage; more samples reduce filtering
noise and take longer offline. Bake once and reuse the result across assets.
Both tools overwrite their output paths; use a new filename to preserve an old
bake. To generate the original procedural studio without an external image:

```sh
/tmp/sf-environment-venv/bin/python tools/generate-environment.py assets/environments/my-studio.sfenv
```

Ship the resulting `.sfenv` with your application and select it on a PBR asset:

```js
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true});
setModelLighting(asset, {
  environment: 'assets/environments/custom.sfenv',
  environmentIntensity: 1,
  environmentRotation: 0,
  ambient: 0,
});
```

Paths resolve from the engine's working directory. Copy the bake to the same
relative path on the headset; Python and the source panorama are not needed
there. Adjust intensity and rotation without rebaking. The environment lights
materials but does not draw a background. Paths are cached while referenced: use
a new path when replacing a bake in a running scene. Scene saves reference the
path, so retain the file for later restores. Keep personal source images,
intermediate arrays and generated custom bakes out of Git unless intentionally
shipping them as licensed application assets.

[The studio source and provenance](../assets/environments/README.md) are included.

## Current limits and performance

This is a direct-light PBR implementation, not a complete glTF material renderer.
There are no shadows, live reflection probes, local/parallax-corrected
reflections, punctual-light imports, bloom, transmission, clearcoat, physical
sheen, or other advanced material extensions. Unsupported optional material extensions use the core
metallic/roughness fallback. Metals can look dark away from their direct-light highlight when an environment
is disabled. Environment lighting must be explicitly selected.

Static opaque lit geometry supports [automatic model LOD](javascript-api.md#automatic-model-lod)
by default. It preserves full meshes and surface seams while adding shared,
reduced expanded streams for distant objects. `{lod: false}` disables generation.
Optional reduced PBR buffers share a 32 MiB engine-wide payload budget; exhausted
budget or failed optional allocations keep finer/full geometry. This budget does
not include full meshes, textures, environments or overlays. Full detail returns
nearby and while held, with textures and picking unchanged.

External occlusion uses a cheaper depth pass automatically: opaque parts write
solid depth, masked parts retain their alpha-cutout holes, and blended parts do
not act as external depth occluders. This preserves the existing overlap policy
and does not change material quality or require a script option.

`optimizeVertexCache: true` enables full-input indexing and cache/fetch ordering
for eligible static opaque/masked lit primitives when compact buffers save space.
The option defaults to `false`; coplanar overlapping triangles can depend on their
original order. Position, both UV inputs, normal and tangent/handedness all
participate in identity, preserving surface seams. Full-detail buffers become
compact while reduced LOD streams retain their existing quality and ownership.
Transparent and deforming primitives keep expanded uploads. If optional indexing
cannot be staged, the mesh keeps its expanded buffers. Reduced levels currently
remain expanded: a lower triangle count does not guarantee less GPU time than
cache-indexed full detail. Profile the intended model workload when choosing LOD.

Lit animation still uses CPU position/surface deformation. The legacy position/UV
indexer, ordered instancing, rigid part batching and GPU skinning do not run on
lit assets. Loading options for those other policies remain stored but do not enable
those paths for PBR. Additional surface/LOD buffers and material textures are
included in the engine's GPU allocation estimate. Dense-scene cost still depends
on model complexity, materials and overlay layout.

Scene-container snapshots preserve the PBR choice and lighting settings. Restore
rejects a conflicting cached asset instead of changing existing objects' shared
lighting. V1–v5 snapshots restore unlit models; v6 retains direct PBR lighting without an
environment. See [scene persistence](scene-persistence.md).

See [pbr.js](../scripts/pbr.js) for a concise interactive example. The separate
[Khronos material test scene](../tests/scenes/pbr-materials.js) covers the lighter
diagnostic models without capturing controller input. The two high-triangle
metal/roughness grids stay in offscreen regression checks.
