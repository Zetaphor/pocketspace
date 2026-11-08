# External depth clusters

Measured 2026-11-07 on Steam Frame with the headset active, all eight demos running
together, using `tools/benchmark-demos.py --captures all --repeat 3` (15 s settle,
30 one-second samples per run). The wearer used the headset normally rather than
holding still, so each figure is the median of three runs with their range.

## Where the GPU time went

Two experiments separated overlay count from external-occluder depth passes. Both
used temporary changes that were never committed:

- **Compact groups**: copies of the demos placing every object below 0.6 m scale in
  one render group per demo, leaving floors, desks and the turntable independent.
- **No occluder depth**: an engine build that skipped every external depth pass.
  Occlusion between surfaces was wrong; it only bounds what the passes cost.

| | Reference | Compact groups | No occluder depth |
|---|---|---|---|
| FPS | 39.5 (38.2–43.2) | 48.9 (47.3–53.3) | 66.8 (64.2–67.1) |
| Engine CPU (one core = 100%) | 52.0% (51.8–55.4) | 45.5% (41.2–45.8) | 51.8% (51.4–52.4) |
| Engine GPU busy | 50.0% (49.9–50.8) | 56.0% (52.9–58.6) | 43.5% (42.0–45.0) |
| Engine GPU time per frame | 12.7 ms | 11.5 ms | 6.5 ms |
| Compositor GPU busy | 42.6% (39.6–44.0) | 36.9% (36.3–38.2) | 36.0% (35.6–37.6) |
| Total GPU busy at 903 MHz | 98% (95–99) | 98% (94–100) | 84% (83–88) |
| GPU completion wait per frame | 18.4 ms | 14.9 ms | 10.8 ms |
| Engine GPU estimate | 571–584 MiB | 779–816 MiB | 567–588 MiB |

Engine GPU time per frame is engine GPU busy divided by FPS.

- External depth passes were about half of the engine's GPU time per frame.
- Grouping cut compositor GPU busy by about six points, but the groups drew more
  pixels and used about 220 MiB more GPU memory. Several groups were also limited
  by the 512 MiB group buffer budget or the 4,096-pixel cap, so that variant
  rendered some content below its normal density. A first variant that grouped
  every object, floors included, exhausted the budget and stopped the engine.
  The engine now creates such groups at the size that fits.

A temporary per-frame draw counter (also not committed), with all demos running:

| Scene | Depth draws | Depth triangles | Color draws | Color triangles |
|---|---|---|---|---|
| lighting-studio | 101 | 848k | 46 | 284k |
| desk-toys | 140 | 134k | 30 | 27k |
| basketball | 24 | 49k | 22 | 41k |

Depth passes drew about 74% of all triangles. The ToyCar alone accounted for 714k
depth triangles per frame: 6.6 full-detail draws of 108k triangles each, while it
covered only part of each neighbouring surface. Scissoring limits fragment work
but not vertex work.

## Clustered depth draws

Static opaque/masked primitives of at least 4,096 triangles keep a second index
buffer with their full-detail triangles sorted along a Morton curve of their
centroids and split into 256-triangle clusters. In an external depth pass, each
cluster box is tested with the same projection and `mayOcclude` test as its
object; adjacent passing clusters are drawn together. Culled clusters lie
outside the target's conservative bounds or behind it, so the target's depth
does not change.

| | Reference | Clustered depth |
|---|---|---|
| FPS | 39.5 (38.2–43.2) | **47.8** (43.4–48.6) |
| Engine CPU (one core = 100%) | 52.0% (51.8–55.4) | 59.6% (53.8–61.0) |
| Engine GPU busy | 50.0% (49.9–50.8) | 48.9% (48.2–52.4) |
| Engine GPU time per frame | 12.7 ms | 10.2 ms |
| Compositor GPU busy | 42.6% (39.6–44.0) | 38.5% (38.2–44.4) |
| Total GPU busy at 903 MHz | 98% (95–99) | 93% (92–99) |
| GPU completion wait per frame | 18.4 ms | 14.8 ms |
| Engine GPU estimate | 571–584 MiB | 569–596 MiB |

- FPS rose about 21%, beyond the run-to-run range, and engine GPU time per frame
  fell by about 2.5 ms, about 40% of what removing the passes entirely saved.
- Engine CPU rose by about seven points, from more frames and the per-cluster
  bound projections. The extra index buffers add roughly 12 MiB.
- `render-occlusion-regression` checks that every triangle is kept once with its
  winding, that cluster bounds are conservative, and that drawing only the
  clusters overlapping a region gives exactly the same pixels there, for indexed
  and unindexed streams.
- The user confirmed in the headset that occlusion looked correct, including
  lighting-studio's turntable pieces passing in front of each other.

## Cluster threshold

After [shadow view culling](shadow-view-culling.md), a temporary draw counter
showed about 586k depth triangles per frame with all demos, against 364k for
color: 248k from ToyCar clusters that overlap their targets, and about 290k from
primitives below 4,096 triangles drawn whole (cradle balls, material spheres,
basketballs, lantern). Lowering the threshold to 1,024 triangles cut depth
triangles to about 470k, but was slower in alternating three-run comparisons
(measured 2026-11-07, all demos):

| | 4,096 triangles (kept) | 1,024 triangles |
|---|---|---|
| FPS | 49.5 (48.7–50.7) | 46.2 (45.5–47.4) |
| Engine GPU busy | 49.4% (48.9–49.6) | 50.2% (49.9–51.1) |
| Engine GPU time per frame | 10.0 ms | 10.9 ms |
| Engine CPU (one core = 100%) | 59.0% (58.0–60.2) | 54.6% (52.9–57.0) |

Splitting small primitives into several draws costs more than the vertex work it
saves, so the threshold stays at 4,096 triangles. Remaining depth cost comes from
those small primitives and from clusters that do overlap their target.

## Automatic grouping

A build put every object created without a `renderGroup` option and below 0.6 m
scale into one engine-owned group per scene, splitting into up to four tiles with
the default policy; larger objects stayed independent. With all eight demos
(three runs each, 2026-11-07, after shadow view culling):

| | Independent by default | Automatic grouping |
|---|---|---|
| FPS | 49.5 (48.7–50.7) | 49.6 (49.2–50.4) |
| Engine CPU (one core = 100%) | 59.0% | 40.2% |
| Engine GPU busy | 49.4% | 59.3% |
| Compositor GPU busy | 42.0% | 37.6% |
| Engine GPU estimate | about 575 MiB | 759–789 MiB |

Fewer overlays and depth passes saved compositor GPU and engine CPU time, but the
groups rendered more pixels than the independent surfaces they replaced, so the
GPU-bound frame rate did not change and GPU memory rose by about 190 MiB. The
change was not adopted. Grouping that pays off would need group pixel counts no
larger than the members' independent crops.
