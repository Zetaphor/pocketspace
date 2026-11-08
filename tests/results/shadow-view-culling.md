# Shadow view culling

Measured 2026-11-07 on Steam Frame with the headset active, using
`tools/benchmark-demos.py --captures each --demos lighting-studio --repeat 3`
(lighting-studio alone, 15 s settle, 30 one-second samples per run). The wearer
used the headset normally. `getEngineStats().shadow.gpuMs`, the shadow pass's own
timer query, was read through `pocketspace-cli eval` every two seconds after each
run had settled.

Lighting-studio has one shadowed point light at 256 pixels per face. Before this
change, every visible opaque or masked caster was drawn into all six faces at full
detail, including the 108k-triangle ToyCar, and scissoring or clipping removed
only fragment work.

Now each face skips casters whose transformed bounds lie wholly outside one of its
clip planes. Static casters with [depth clusters](depth-clusters.md) test each
cluster the same way and draw only the clusters that reach the face. Geometry
outside a view rasterizes nothing in it, so the map is unchanged. The same test
applies to the single view of spot and directional lights.

Builds were alternated: baseline, culling, baseline again.

| | Baseline (first) | Culling | Baseline (second) |
|---|---|---|---|
| FPS | 74.7 (74.2–75.5) | **79.3** (78.7–80.5) | 74.1 (73.7–75.9) |
| Shadow pass GPU ms (median of runs) | — | **0.18** (0.179–0.186) | 0.72 (0.686–0.739) |
| Engine CPU (one core = 100%) | 51.7% | 47.8% | 50.9% |
| Engine GPU busy | 47.4% | 47.5% | 49.3% |
| Compositor GPU busy | 28.2% | 27.9% | 27.9% |
| Total GPU busy at up to 903 MHz | 79% | 82% | 84% |
| GPU completion wait per frame | 9.90 ms | 8.55 ms | 10.00 ms |

The first baseline's shadow poller failed, so its GPU time is missing.

- The shadow pass's GPU time fell by about 75%, and FPS rose by about 7%, beyond
  the range of both baselines.
- The roadmap's earlier figure of about 3 ms per frame came from a different
  measurement; this method gives 0.72 ms before the change.
- `spot-shadow-regression` renders a 12,800-triangle clustered caster with
  culling disabled through `shadowViewValidation::cull` and enabled, and requires
  identical receiver images for spot, directional and point lights. In point mode
  three of the six views skip the caster entirely and 70 of the remaining 150
  cluster tests are culled.
