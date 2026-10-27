# Opt-in render group subdivision

Measured on Steam Frame on 2026-10-26 with the implementation introducing this
file (parent revision `bf4fc86`). Native engine runs compare the same two-island
scene with subdivision disabled/enabled. Each has 16 bodies and a separate floor,
preloads boxes, ToyCar, and Avocado, and uses fixed kinematic poses, the same HUD,
and unchanged member sampling density. Asset caches are identical between runs.

The script requests at most four tiles for logical group 1, with a 1024-pixel
trigger. Both adaptive cases selected two body tiles. The existing single group
remained one body tile. No body tile reached the resolution ceiling.

| Geometry | One body tile FPS | Adaptive FPS | One tile GPU estimate MiB | Adaptive GPU estimate MiB |
| --- | ---: | ---: | ---: | ---: |
| Colored boxes | 89.8 | 90.0 | 274.2 | 110.2 |
| ToyCar / Avocado | 84.5 | 89.7 | 274.2 | 110.2 |

[Raw summary](auto-split-frame.json) records FPS, CPU, RSS, GPU estimates, sample
counts, and tile counts. Runs lasted 12 seconds, discarded the first five seconds,
and alternated pair order between geometries. Each retained 13–14 samples.
GPU allocation estimates describe engine-owned resources, not dedicated VRAM.
These are short, scene-specific measurements; models used more CPU when split,
and larger scenes or different layouts may have different throughput.

The planner reported allocated area dropping from 6,553,600 to 1,179,648 pixels
squared for the body group. The total GPU estimate includes the unchanged floor,
assets, and panel. Child tiles retain the normal 25% allocation headroom and all
members' requested angular sampling density.

## Validation

- Actual GLES pixel checks preserve floor/body occlusion across internal tiles
  within the same logical group, both eyes, and lateral viewpoints. Matching-grid
  images match the existing separate-group baseline exactly. Common-plane math
  checks cover differing tile origins and late head motion.
- CPU planner checks cover complete/stable partitions, allocation savings,
  thresholds, compact/single-member fallbacks, and invalid near-plane bounds.
- Real JavaScript binding checks cover options/defaults, nearest-tile creation,
  disabling future splits, and preservation of physics/grab IDs.
- Mock presentation checks inject texture/show failures into either replacement,
  plus a child-hide failure and either/both replacement tiles culled. The complete
  parent remains available; buffers are
  gated on successful hiding before reuse, and retries retire the parent only
  after successful presentation.
- An 18-second native lifecycle run rebuilt wide boxes, wide models, compact
  models, wide boxes again, cleared bodies, rebuilt models, hid all members,
  then showed them while changing the split policy. Four repeated splits
  completed; 1,550 frames finished with normal SIGTERM cleanup and reported
  `gl_error=0`. No persistent demo was left running.
- Existing GLES, panel, projection-cache, examples, script-lifecycle, and grab
  regressions passed; documentation lint passed.

Headset visual testing was accepted on 2026-10-26 after testing instructions
covered head motion, grabbing, boxes/models, and compact/wide rebuilds. The user
reported "Looks good." Keep subdivision opt-in. Stable tiles do not merge or
rebalance after objects move, and a single large object can still exceed the
resolution ceiling.
