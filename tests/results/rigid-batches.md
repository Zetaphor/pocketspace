# Adjacent identical-material rigid batches

Measured on Steam Frame on 2026-10-28 at `1eb9172`. This isolated prototype shows
a substantial throughput gain on the targeted rigid workload. Production
integration and removal of duplicate prototype storage/uploads remain unfinished.
Normal builds compile out `SF_RIGID_BATCH_EXPERIMENT`.

## Candidate and constraints

Nine consecutive InterpolationTest cube parts have identical untextured
materials; the last part is a textured plane. Concatenate current expanded CPU
vertices in original part/triangle order, then draw each adjacent identical-state
run once. The plane remains a separate draw. Nothing crosses an object or
material boundary; no sorting or multi-material shader is introduced.

Eligibility is limited to animated CPU rigid assets without skins/morphs and with
complete triangles. Runs require untextured parts (the engine's immutable white
texture), equal base factors, sheen, alpha mode/cutoff and double-sided state.
Blended parts, textured parts and any state change break runs. Static assets,
skins and morphs retain existing paths. Material state is rechecked at draw time.
The immutable-model-texture assumption excludes unsupported external GL mutation.

For this controlled prototype, both modes retain original per-part VBOs **and**
a per-pose combined VBO. Changed ranges update both, so baseline/batched storage
and upload work match. The combined stream contains exact current normalized
vertices/UVs; CPU bounds/BVH and picking geometry remain unchanged. Pose teardown
deletes the extra VBO, which is included in engine GPU allocation estimates.
This is not the proposed final production memory/upload design.

## Correctness

Workstation and Frame passed 864 **exact** stereo image comparisons across Box,
ToyCar, AlphaBlendModeTest, InterpolationTest, Fox and AnimatedMorphCube. Tests
exercise opaque/masked/blended states, GPU/CPU/morph fallbacks, instancing program
switches, scale/rotation/translation clip changes and material barriers inserted
inside the nine-part run. They assert merging actually occurs, compare mapped
combined VBO ranges against each current CPU primitive in order, and verify
buffer deletion when poses are removed. The existing 483-view instancing check
and normal engine build also pass locally.

These checks establish current renderer equivalence for sampled assets, not
support for arbitrary materials or user visual acceptance of the prototype.

## Measurement method

- ARM64 Steam Frame, zink/Turnip Adreno 750, GLES 3.2 Mesa 26.3.0-devel
  (git-abc426bb62); no power/governor settings changed.
- Same executable/scene, `SF_RIGID_BATCH=0` then `1`. 256 independent
  InterpolationTest actors, Linear Scale clip 1, same 16x16 grid and scale,
  768 reference eye pixels, one render group, one HUD, no physics. Production
  rigid primitive caching is enabled in both modes.
- Each mode runs 20 seconds animated then 20 paused. User was asked to keep
  looking at the models; both runs completed and timed processes were stopped.
- Frame-weight 120-frame windows starting after five seconds warmup. Ordinary
  mode retains two windows (240 frames) per stage; batched mode eight (960).
  One matched pair, not a long-session or broad-asset performance claim.
- Timings are CPU elapsed ms/frame; completion wait is not GPU timestamp time.

## Results

| Mode | Playback | FPS | Upload submission | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- | --- |
| Ordinary | Animated | 20.31 | 0.666 | 20.722 | 25.655 | 49.227 |
| Batched | Animated | 68.23 | 0.556 | 3.367 | 9.228 | 14.648 |
| Ordinary | Paused | 20.72 | 0 | 21.432 | 25.910 | 48.247 |
| Batched | Paused | 71.62 | 0 | 3.466 | 10.017 | 13.953 |

Animated throughput increases 3.36 times; paused throughput 3.46 times.
CPU-pose primitive draws fall 80%, from 5,120 to 1,024 per frame across both eyes:
512 nine-part batches plus 512 separate planes. Counters report 4,608 source
parts in merged runs (4,096 draws saved). CPU render submission falls about 84%.
Wait duration also falls, but it is not an isolated GPU execution measurement.

All stages end at **214,725,652 bytes** estimated engine GPU allocation.
Animated stages match at 9,216 updated CPU vertices and **368,640 uploaded
bytes/frame**, including the deliberately mirrored stream updates. The prototype
adds 1,689,600 bytes of GPU storage over per-part geometry for these 256 actors.
Endpoint animated RSS is about 75.8 MiB ordinary and 75.2 MiB batched; this does
not establish memory savings. Driver VRAM totals are unavailable. Matching
allocations do not fix live head pose/animation timing, but there is no quality,
shader, material or geometry reduction.

## Adoption work and evidence

The user confirmed correct clip switching/blending, A/X pause/resume and
grabbing in headset on 2026-10-28, using `scripts/blending.js` with batching
enabled in the `1eb9172` experiment executable. The demo was stopped after
feedback. This acceptance applies to the prototype.

Before production adoption, replace the duplicate prototype VBO arrangement with an owned stream design that keeps
partial-update caching, fallback draws, resource accounting, reload/restore and
precise picking intact. Recheck exact images and performance after that change.
Do not generalize the measured gain to textured/PBR or mixed-material models.

- [Run instructions](../README.md#adjacent-rigid-primitive-batching)
- [Ordinary log](rigid-batches/full-frame.txt), [summary](rigid-batches/full-summary.json)
- [Batched log](rigid-batches/on-frame.txt), [summary](rigid-batches/on-summary.json)
- [Frame image check](rigid-batches/pixels-frame.txt)

Scene/analyzer checks, documentation lint/site build and whitespace checks pass.
