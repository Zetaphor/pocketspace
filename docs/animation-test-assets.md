# Animation test asset

Selected fixture: `assets/samples/InterpolationTest/InterpolationTest.glb`.
Upstream: [Khronos InterpolationTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models/InterpolationTest).
The unmodified 7,952-byte GLB and its upstream README are included under the
existing sample repository revision, with credits/license in
[asset attribution](../assets/samples/ATTRIBUTION.md). License: CC0, credited to Khronos.
Git blob: `4200fccc95261f36bf8af58e99f3180968333714`.

## Coverage

The downloaded binary contains nine named animation clips, two meshes, no skins,
and no required extensions. Every clip has one channel and spans two seconds (0–2 seconds):

| Target | STEP clip | LINEAR clip | CUBICSPLINE clip |
| --- | --- | --- | --- |
| Translation | Step Translation | Linear Translation | CubicSpline Translation |
| Rotation | Step Rotation | Linear Rotation | CubicSpline Rotation |
| Scale | Step Scale | Linear Scale | CubicSpline Scale |

Start playback implementation with the three LINEAR clips. Rotation exercises
quaternion interpolation; translation and scale exercise vector interpolation.
Then use the STEP and CUBICSPLINE clips to verify the additional interpolation
rules. Independent per-object playback can later be checked by creating several
instances of this shared asset with different clips/times. Skeletal playback now uses the existing `CesiumMan` fixture;
`MorphPrimitivesTest` remains available for the later morph implementation.

## Current support and validation

Node TRS playback is now implemented; see the [animation API](animation-api.md)
for controls, limitations, and tests. Loading alone still shows the static pose;
call `playAnimation` to start a clip. Skeletal skinning is now supported as well;
morph playback remains deferred.
The GLB's Git blob hash was compared with GitHub's metadata at the pinned revision;
the local JSON chunk was inspected for clip paths, interpolation, durations,
skins, and extensions. Deckard's existing engine `--inspect` loaded it successfully:
10 rendered parts, 110 triangles, animation present, no skins or morph targets.
This inspection was CPU-only and did not launch an overlay or prove animation
playback. The two source meshes are `Cube` and `Plane.001`.

The wearer tested `scripts/animation-test.js` on `b53c9d6` and confirmed all nine
clips work. See [recorded verification](animation-api.md#recorded-verification)
for scope and logs.
