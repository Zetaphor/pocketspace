# Spotlight shadow validation

Steam Frame, October 30, 2026. Opt-in shadows change lighting appearance;
these measurements describe their cost, not an equivalent-output optimization.

## Automated correctness

Native checks passed: `spot-shadow-regression`, `spotlight-regression`,
`point-light-regression`, `directional-light-regression`,
`scene-persistence-regression`, `asset-lifetime-regression`,
`examples-regression`, `pbr-regression`, `environment-regression`,
`sheen-regression` and `animation-blend-regression`.

Coverage includes atomic API validation, single-light capacity, v12 snapshot
compatibility/conflicts, stereo, masked silhouettes, hidden/blended exclusion,
cross-group casters, prepared/general shaders, animated CPU/GPU geometry,
GL-state restoration, work/time limits, explicit retry, reload and map cleanup.
The examples check covers 43 scripts; existing PBR checks cover 130 images.
The frozen point/spot reference predates sheen: its exact-pixel comparison
uses zero sheen factors, then restores them for current mixed-light checks.
Dedicated sheen checks cover the physical layer.

The final watchdog correction was rerun through `spot-shadow-regression`.
Deterministic checks ensure a brief 4 ms sample among .02 ms samples does not
trip a 2 ms limit, sustained 3 ms samples do, and reset clears accumulated
history. Native rendering checks still exercise actual GPU-budget fallback.

## Bounded performance

The offscreen benchmark captures no pointer input and creates no overlays.
One/four ToyCars share a PBR asset with cache-optimized indices and full-detail
casters. Direct spotlight lighting uses no environment. Each stage warms 12
frames and measures 36, in disabled/enabled/enabled/disabled order. Map sizes
are 256/512/1024; the color pass renders one alternating eye at 768 pixels.
The profiling threshold is 16 ms. GPU-disjoint and GL-error checks passed.

Means of the paired stages, in milliseconds; memory is estimated depth payload:

| Cars | Map pixels | Shadow GPU | Color GPU off/on | Completed map + one eye off/on | Map MiB |
| --- | --- | --- | --- | --- | --- |
| 1 | 256 | .089 | .355 / .326 | .871 / 1.218 | .25 |
| 1 | 512 | .132 | .353 / .367 | .695 / 1.076 | 1 |
| 1 | 1024 | .113 | .440 / .354 | 1.070 / 1.346 | 4 |
| 4 | 256 | .481 | 1.154 / 1.098 | 2.325 / 3.357 | .25 |
| 4 | 512 | .576 | .978 / 1.225 | 2.181 / 3.213 | 1 |
| 4 | 1024 | .714 | .900 / .862 | 2.079 / 2.628 | 4 |

These are bounded pass costs, not full stereo compositor FPS. Scheduling
variability prevents drawing monotonic conclusions between resolutions.
The profile precedes the watchdog averaging correction, which changes cutoff
policy rather than map rendering. GPU results arrive asynchronously.

## Interactive acceptance

The final normal-engine example uses independent surfaces for its compact
platform, caster and light handle. It ran at approximately 90 FPS with no GL
errors. The original single-sample watchdog reacted to roughly 4 ms timing
spikes and removed shadows. The final policy ignores three warmup updates,
then requires a full 24-valid-result rolling average above the configured limit.

After that correction the user confirmed that shadows stay visible and work
correctly while moving the caster/light and testing enable/resolution controls.
The demo was stopped and its exit verified after acceptance. This is visual
acceptance for the sample scene, not a guarantee for every scene scale or bias
setting. One spotlight, opaque/masked casters and bounded map/work limits remain
the documented scope; point/directional shadows are still backlog work.
