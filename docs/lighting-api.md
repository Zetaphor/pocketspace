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
  environment: 'assets/environments/studio.psenv',
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
| `environment` | Path to a baked `.psenv` lighting environment; `null` disables it | Disabled; nonempty string up to 4096 bytes, or `null` |
| `environmentIntensity` | Diffuse and specular environment multiplier | `1`; 0 through 64 |
| `environmentRotation` | Rotate the environment around scene +Y, in radians | `0`; −10000 through 10000 |

Direction is normalized by the shader. All numeric values must be finite.
These per-asset settings provide one unshadowed directional
light, constant diffuse ambient light and an optional baked environment per asset.
Scene point, spot and directional lights below add illumination independently of those settings.
Emissive materials do not illuminate other objects.

Configure lighting on a shared asset and rotate a lit model: [pbr.js](../scripts/pbr.js).

## Dynamic point lights

Create scene-wide point lights, sharing a limit of eight lights with spotlights and scene directional lights. They illuminate every PBR model,
including animated models, in addition to its existing per-asset directional,
ambient and environment lighting. Unlit assets, `KHR_materials_unlit` materials,
panels and laser geometry ignore them. Lights do not draw visible bulbs or cast
shadows; create a marker object separately if needed.

```js
const tool = createSceneContainer();
const marker = createObject(loadModel('assets/physics/YellowSphere.glb'),
  -.25, .15, -.15, .035, {container: tool});
enableObjectInteraction(marker, {grabButton: 'both'});
const light = createPointLight({
  objectId: marker, container: tool,
  color: [1, .25, .08], intensity: .3, range: 1.5
});
setPointLight(light, {enabled: false});
setPointLight(light, {enabled: true, position: [0, .05, 0]});
print(JSON.stringify(getPointLight(light)));
// removePointLight(light); // Removes illumination, leaving the marker in place.
```

Run [lights.js](../scripts/lights.js) to carry a colored light around a car,
change its color, toggle it, and capture/restore the lit tool.

| Function | Behavior |
| --- | --- |
| `createPointLight(options = {})` | Return a fresh positive light ID. Up to eight live point/spot/directional lights combined per scene, including disabled lights. |
| `setPointLight(id, options)` | Update only supplied fields. Invalid options throw before publishing the update. |
| `getPointLight(id)` | Return a detached snapshot `{id, type: 'point', position, color, intensity, range, enabled, objectId, container}`. |
| `removePointLight(id)` | Remove a live light; unknown or removed IDs throw. |

| Option | Default | Meaning and limits |
| --- | --- | --- |
| `position` | `[0, 0, 0]` | Three finite numbers from −10000 through 10000, in meters. Scene position when unattached; object-local offset when attached. |
| `color` | `[1, 1, 1]` | Linear RGB, three finite components from 0 through 64. |
| `intensity` | `1` | Finite radiance multiplier from 0 through 64; inverse-square attenuation makes close lights much brighter. This is not a calibrated lumen value. |
| `range` | `1` | Finite cutoff distance in meters, .001 through 64. Light fades smoothly to zero at this distance. |
| `enabled` | `true` | Boolean switch. Disabled lights retain their ID and settings. |
| `objectId` | `null` | Live object ID to follow, or `null` to detach. |
| `container` | `0` | Active cleanup scope; zero is root ownership. Updating it transfers ownership. |

Attached offsets use object-local +X right, +Y up and +Z forward. They rotate
with the object but remain in meters regardless of its model scale. Attachments
follow the object after interaction/physics updates; visibility does not switch
off a light. Detaching preserves the stored numeric `position`, which then
becomes scene-relative; set a new position in the same call to avoid a jump.
Removing the attachment object removes its lights even if owned by another
container. Disposing a container removes its lights; moving an object to another
container does not transfer light ownership automatically.

Point lights use the same GGX direct-light BRDF as the existing directional
light. Attenuation is `(1 - (distance / range)^4)^2 / max(distance^2, .0001)`
inside the range, and zero outside. The minimum denominator avoids a singularity
at the light position. Lighting and exposure can saturate near bright lights;
reduce intensity rather than relying on the range to control nearby brightness.

Snapshots v8 and later save owned lights and return fresh `lightIds`/`lightMap` on restore.
An attached light and its object must belong to the captured container; crossing
that boundary throws rather than saving a dangling reference. Light attachments
remap to restored object IDs. Root lights are not captured. Older v1–v7 snapshots
restore without point lights. Restore stages the whole light set and requires
room within the eight-light limit while the original tool is still live.

With four or more active, nonzero lights, the engine conservatively excludes
lights whose range cannot reach an object's current full/deformed bounds.
Spotlights are also excluded when their entire cone misses the padded sphere
enclosing those bounds. This automatic filter preserves light order and is
shared by both eyes, LODs and surfaces; it does not reduce range, cone width or
quality. Objects close to the cutoff retain the light, and invalid bounds retain
all lights. Objects touching the cone edge or surrounding the light origin keep
the spotlight; hemispherical/wider or invalid cones skip CPU cone rejection.
It is most useful when several local lights illuminate different parts of a
scene. Lights whose ranges and cones reach every object keep the full list.
Retained spotlights use the same per-fragment cone falloff in the shader.

The renderer uses a bounded forward loop over the remaining enabled, nonzero
lights for each lit fragment. More lights and more screen coverage increase GPU
work; there is no tiled-light system or shadow pass. The external-depth pass
skips light setup. Start with one or two lights and profile your intended scene
before using all eight.

## Dynamic spotlights

Spotlights illuminate a cone rather than all directions. They share the same
PBR receivers, intensity/range behavior, attachments, container cleanup and
eight-light scene budget as point and directional lights. They can opt into
[spotlight shadows](#spotlight-shadows). They do not draw a visible beam through
empty space; a cone appears where it illuminates geometry.

```js
const flashlight = createSpotLight({
  position: [0, .1, -.5], direction: [0, 0, 1],
  color: [1, .95, .85], intensity: .3, range: 2,
  innerConeAngle: .25, outerConeAngle: .45
});
setSpotLight(flashlight, {direction: [.2, -.1, 1]});
print(JSON.stringify(getSpotLight(flashlight)));
// removeSpotLight(flashlight);
```

| Function | Behavior |
| --- | --- |
| `createSpotLight(options = {})` | Create a spotlight and return its fresh light ID. |
| `setSpotLight(id, options)` | Stage supplied fields and publish only after validation succeeds. |
| `getSpotLight(id)` | Return the common light fields with `type: 'spot'`, plus `direction`, `innerConeAngle` and `outerConeAngle`. |
| `removeSpotLight(id)` | Remove a live spotlight, leaving its marker/attachment object intact. |

These functions accept spotlight IDs only; the point-light functions accept
point-light IDs only. Passing the wrong type throws without changing the light.
Type is fixed at creation. All common point-light options apply, with these
additional spotlight fields:

| Option | Default | Meaning and limits |
| --- | --- | --- |
| `direction` | `[0, 0, 1]` | Direction **away from** the light. Three finite components from −64 through 64; vector length at least .000001. Normalized for rendering; the getter retains the supplied components. |
| `innerConeAngle` | `.35` | Fully illuminated half-angle in radians, from zero through `outerConeAngle`. |
| `outerConeAngle` | `.6` | Outer half-angle in radians, from .001 through pi/2. Illumination is zero beyond this angle. |

For unattached lights, direction uses scene axes (+Z forward). When `objectId` is
set, both position and direction use object-local axes and rotate with the object;
direction is unaffected by object translation or scale. Detaching keeps both
stored vectors, now interpreted in scene axes. Update them in the same call if
needed. A partial cone update must leave `innerConeAngle <= outerConeAngle`.
Equal angles produce a hard edge.

Within the cone, angular attenuation is
`clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0, 1)^2`, multiplied by the
point light's distance attenuation. The equal-angle case uses a step instead of
dividing by zero. Cone width does not renormalize intensity: narrowing the beam
keeps the center brightness unchanged.

Snapshots v9 and later store a `type` for every light and the spotlight direction/cones.
New captures use v12, which also preserves spotlight shadow settings.
Restore returns the same shared `lightIds`/`lightMap` and remaps attachments.
V8 snapshots retain their point-light behavior; v1–v7 restore without scene
lights. Ownership and cross-container snapshot rules are identical for all scene light types.

Run [spotlights.js](../scripts/spotlights.js): grip/trigger grabs the blue handle,
bumper switches cone width, A/X toggles the light, and B/Y captures then restores
the tool. Rotate the handle to aim the beam across the car. Cone rejection can
avoid BRDF work outside the beam, but adds direction/cone math; profile the
intended scene rather than assuming a spotlight is faster than a point light.

## Spotlight shadows

Spotlights can opt into a shared light-space depth map. Shadows affect only that
spotlight's contribution to PBR materials: model lights, other scene lights,
ambient/environment illumination and emissive colors remain independent. Unlit
models can cast shadows but retain their unlit appearance. Panels and lasers do
not cast or receive shadows.

```js
const flashlight = createSpotLight({
  position: [0, .6, -.6], direction: [0, -1, 1], range: 3,
  intensity: 2, innerConeAngle: .3, outerConeAngle: .8,
  shadow: {enabled: true, resolution: 512}
});
setSpotLight(flashlight, {shadow: {resolution: 256}});
print(JSON.stringify(getSpotLight(flashlight).shadow));
print(JSON.stringify(getEngineStats().shadow));
setSpotLight(flashlight, {shadow: {enabled: false}});
```

`createSpotLight` and `setSpotLight` accept a partial `shadow` options object.
`getSpotLight` returns the full settings, even when shadows are disabled.
Point and directional lights reject the `shadow` option.

| Shadow option | Default | Meaning and limits |
| --- | --- | --- |
| `enabled` | `false` | Boolean opt-in. At most one enabled spotlight can have shadows enabled, including zero-intensity lights. Disabled lights retain their configuration without occupying the shadow slot. |
| `resolution` | `512` | Square depth-map size: 256, 512 or 1024. Active depth storage is estimated at 0.25, 1 or 4 MiB respectively; driver/program overhead and in-flight retired storage are additional. |
| `near` | `.05` | Near clipping distance in meters, .001–1, strictly less than light range when shadows are enabled. Geometry before this plane does not cast into the map. |
| `bias` | `.0005` | Normalized depth comparison offset, 0–.05. Larger values reduce acne but can separate shadows from objects. |
| `normalBias` | `.002` | Receiver offset along its shading normal, in meters, 0–.1. |
| `maxTriangles` | `500000` | Full-detail caster-triangle budget, integer 1–1000000. The pass also permits at most 128 visible caster objects. |
| `maxGpuMs` | `2` | GPU-time watchdog threshold, .001–16 ms. After three warmup updates, a rolling average of 24 valid elapsed-time results above the threshold disables the shadow pass until explicitly retried. |

A shadow-enabled light requires `outerConeAngle <= 1.5` radians. All supplied
settings are validated before publishing; invalid updates and shadow-slot
conflicts leave the previous light unchanged. To transfer the slot, disable
shadows on the old light before enabling them on another.

The map updates once per engine frame using current object/animation poses,
independently of eye, render group or overlay method. Both eyes sample the same
map. Static, rigid-animated, CPU-deformed and GPU-skinned meshes cast from their
full geometry. Opaque surfaces cast solid shadows; alpha-mask surfaces preserve
texture holes and alpha cutoff. Alpha-blended surfaces do not cast shadows in
this version. Hidden objects do not cast. glTF sidedness and mirrored winding
are honored. Receivers use four comparison-filtered taps; this smooths map edges
but does not simulate area-light penumbrae or transparent shadow transmission.

Geometry-budget exhaustion, failed allocation or unavailable GPU timers produce
unshadowed lighting for that frame; a stale map is never sampled. The GPU
watchdog uses asynchronous `EXT_disjoint_timer_query` results and ignores disjoint
samples. After three warmup updates it requires a full 24-result rolling average
above the limit, so brief spikes alone do not disable shadows. It is a delayed safeguard, not a hard deadline or GPU preemption: warmup
and work queued before a result arrives can exceed the threshold. On a watchdog
trip, the map is released and the pass remains disabled. Retry with a smaller
resolution/workload, or a suitable threshold, by supplying `shadow` options again
(e.g. `{shadow: {enabled: true}}`), or disable/re-enable the light. Geometry-budget
fallback retries automatically when the scene fits again.

`getEngineStats().shadow` reports the most recently processed frame:
`{active, lightId, status, resolution, triangles, gpuBytes, gpuMs}`. `lightId` and
`gpuMs` can be null. Status is `disabled`, `active`, `geometry-budget`,
`gpu-budget`, `timer-unavailable`, `shader-unavailable`, `allocation-failed`,
`unresolved` or transiently `preparing`. `triangles` counts all visible opaque
and masked caster geometry before budgeting, not only geometry inside the cone.
The map's estimated storage is also included once in `gpuAllocatedEstimateBytes`.

V12 snapshots preserve the settings and remap attachments. Older snapshots restore
without shadows. Restoring an enabled shadow light while the slot is occupied
fails transactionally. [shadows.js](../scripts/shadows.js) demonstrates releasing
the old tool's slot, restoring, and recovering its settings on failure. Its HUD
shows current shadow status and time. A/X toggles shadows without turning off the
light, bumper cycles resolution, and B/Y captures then restores the tool.

## Scene directional lights

Use directional lights for sunlight, fill or broad illumination. They affect all
PBR models without distance falloff, in addition to per-asset lighting. Point,
spot and directional lights share one eight-light scene limit, including disabled
lights. They cast no shadows, draw no geometry and do not affect unlit materials,
panels or lasers.

```js
const sky = createDirectionalLight({
  direction: [.4, -.8, 1], color: [1, .9, .75], intensity: 2
});
setDirectionalLight(sky, {direction: [-.4, -.8, 1]});
print(JSON.stringify(getDirectionalLight(sky)));
setDirectionalLight(sky, {enabled: false});
removeDirectionalLight(sky);
```

| Function | Behavior |
| --- | --- |
| `createDirectionalLight(options = {})` | Return a fresh positive ID from the shared scene-light namespace. |
| `setDirectionalLight(id, options)` | Update supplied fields; invalid options throw before publishing. |
| `getDirectionalLight(id)` | Return a detached `{id, type: 'directional', direction, color, intensity, enabled, objectId, container}` snapshot. |
| `removeDirectionalLight(id)` | Remove illumination; leave any attachment object in place. |

| Option | Default | Meaning and limits |
| --- | --- | --- |
| `direction` | `[0, 0, 1]` | Direction rays **travel**, in scene axes: +X right, +Y up, +Z forward. Three finite components from −64 through 64, with length at least .000001; normalized for rendering. |
| `color` | `[1, 1, 1]` | Linear RGB, three finite components from 0 through 64. |
| `intensity` | `1` | Finite multiplier from 0 through 64; no range or inverse-square attenuation. |
| `enabled` | `true` | Boolean switch; disabled lights retain their settings and consume a slot. |
| `objectId` | `null` | Live object to follow, or `null` to detach. Only its orientation affects illumination. |
| `container` | `0` | Active cleanup owner; zero is root ownership. |

Direction matches the spotlight ray convention. It differs from the existing
`setModelLighting().direction`, which points **toward** the per-asset light in
model axes. Attached directions rotate in object-local axes, independently of
translation, visibility and scale. Detaching preserves the stored direction,
now interpreted in scene axes; supply a new direction in the same call if needed.
`position`, `range` and cone fields are unsupported and throw when supplied.
Typed point/spot/directional APIs reject IDs belonging to another light type.

Removal, container transfer/disposal and attachment cleanup follow the point-light
rules. V10 snapshots preserve directional type, direction, color, intensity,
enabled state and remapped attachments. Earlier point/spot snapshots remain
readable; directional entries require v10. Directional lights always contribute
to each eligible PBR object's light set; distance/cone rejection never removes
them. More lights still add per-fragment work, so profile the intended scene.

Run [directional-lights.js](../scripts/directional-lights.js): rotate the blue
handle to aim the key light, move it to see that distance does not change
brightness, use bumper for color, A/X to toggle, and B/Y to capture/restore.

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

View the imported car materials under environment lighting: [pbr.js](../scripts/pbr.js).

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

Load a baked environment and configure its lighting: [pbr.js](../scripts/pbr.js).

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
/tmp/sf-environment-venv/bin/python tools/generate-environment.py assets/environments/custom.psenv --input /tmp/lighting.npy
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
/tmp/sf-environment-venv/bin/python tools/generate-environment.py assets/environments/my-studio.psenv
```

Ship the resulting `.psenv` with your application and select it on a PBR asset:

```js
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true});
setModelLighting(asset, {
  environment: 'assets/environments/custom.psenv',
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

## Fabric sheen

Models loaded with `{pbr: true}` import `KHR_materials_sheen` automatically.
Sheen adds a fabric reflection layer above the metallic/roughness material:

- `sheenColorFactor` is linear RGB; its optional embedded color texture is
  sampled as sRGB and multiplied by the factor. A zero color disables the layer.
- `sheenRoughnessFactor` is independent of base roughness. Its optional embedded
  texture uses the alpha channel as linear data. The default factor is zero.
- Both maps support independent UV0 transforms and glTF sampler settings.
  Unsupported UV sets and invalid factors fail model loading transactionally.

Direct model lighting and scene point, spot and directional lights use the
Charlie microfiber BRDF with Charlie visibility. A shared directional-albedo
lookup attenuates the underlying layer to balance reflected energy. A
roughness-dependent normalization bounds excess grazing energy in the fitted
visibility model while preserving reciprocity. Perceptual
sheen roughness is clamped to 0.07–1 for numerical stability.

Indirect sheen uses the baked environment's diffuse spherical harmonics and
view-dependent directional albedo. This is a low-frequency approximation: sharp
windows and other small HDR lights do not produce separate sheen reflections.
The base material retains its existing GGX environment reflections. Emissive
and unlit behavior remain independent of sheen; non-PBR loading retains the
legacy approximate color tint.

Sheen maps add up to two material textures. Assets share a 32×32 half-float
albedo lookup (4 KiB GPU payload), prepared during asset loading/upload and
released after its last owning asset. Sheen BRDF work applies only to materials
with nonzero sheen color. Model lighting, scene snapshots and asset unloading
use the existing APIs; no script-side sheen override is required.

```js
const fabric = loadModel('assets/samples/CompareSheen/CompareSheen.glb', 768,
  {pbr: true});
setModelLighting(fabric, {direction: [-.6, .3, .7], intensity: 3, ambient: 0});
const object = createObject(fabric, 0, 0, 0, .6);
enableObjectInteraction(object, {grabButton: 'both'});
```

Try [sheen.js](../scripts/sheen.js) to switch between direct and environment
lighting while rotating the material. See the
[Khronos extension](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_sheen)
for authoring parameters.

## Current limits and performance

This is a direct-light PBR implementation, not a complete glTF material renderer.
Shadows are limited to one opt-in spotlight. There are no live reflection probes, local/parallax-corrected
reflections, punctual-light imports, bloom, transmission, clearcoat,
or other unsupported advanced material extensions. Unsupported optional material extensions use the core
metallic/roughness fallback. Metals can look dark away from their direct-light highlight when an environment
is disabled. Environment lighting must be explicitly selected.

Static opaque lit geometry supports [automatic model LOD](javascript-api.md#automatic-model-lod)
by default. It preserves full meshes and surface seams while adding shared,
reduced streams for distant objects. `{lod: false}` disables generation.
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
compact; reduced LOD streams also use cache-ordered compact indices when smaller,
with their existing simplification quality and ownership.
Transparent and deforming primitives keep expanded uploads. If optional indexing
cannot be staged, full detail keeps expanded buffers. Failed reduced-level staging
keeps a finer/full level and releases its optional budget reservation. Profile
the intended model workload when choosing LOD.

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

## Environment file format

Pocketspace lighting environments use `.psenv` files with the `PSENV001`
header. The loader validates dimensions, payload size and finite lighting values
before publishing an environment. The generator requires a `.psenv` output path.

Use the current .psenv environment path in a complete scene: [pbr.js](../scripts/pbr.js).


### Shader preparation

For scenes that start with at least four active lights, the engine prepares a
bounded set of PBR shader variants during scene loading and successful script
reload, up to 16 color programs. Lit draws with at least four contributing lights
and at least 1024 allocated pixels per eye can use ready variants for their
material features and filtered light counts. Lightweight draws, new material/environment combinations
and cache misses use the general shader automatically; drawing never compiles a
new variant. No script option or quality reduction is involved. Existing light
and environment setters keep their normal behavior. See the
[interactive validation scene](../tests/scenes/pbr-variants.js) for mixed materials,
light-count changes and HDR toggles.

```js
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 1024, {pbr: true});
createObject(asset, 0, 0, 0, .3);
for (let i = 0; i < 4; i++) {
  createPointLight({position: [(i - 1.5) * .2, .2, -.4], range: 1, intensity: .1});
}
```
