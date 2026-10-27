# Render tile merging and rebalancing

The policies are opt-in additions to `setRenderGroupAutoSplit`: `merge` and
`rebalance` both default to false. Existing split-only scripts retain their
behavior. The implementation preserves object IDs, logical groups, requested
member sampling density, the common projection plane, and producer completion.

Merging combines two tiles only when the union fits below the configured pixel
trigger and saves at least 20% of steady requested allocation. Rebalancing retains
two tiles but redistributes visible opaque/masked members under the same saving
requirement. Current demand with normal allocation headroom is the baseline;
ordinary delayed buffer shrinking is not counted as an adaptation benefit.
Blended-material pairs defer regrouping to preserve existing compositor order.

A proposal needs three matching observations spanning a second, with a two-second
regroup cooldown after a structural handoff. The pair search is bounded to four
candidates per check, prioritizing a persistent proposal. Hidden-member tiles and
invalid near-plane bounds defer regrouping. This keeps the measured policy
conservative rather than attempting global optimal packing.

## Bounded native comparison

The runner `tests/compare-regroup.py` compares split-only against adaptive policies
on Steam Frame. Both use the same 16 kinematic bodies, separate floor, statistics
panel, loaded asset cache, poses, requested resolution, and object IDs. Live
controller callbacks, grabbing, and lasers are disabled during automation. Runs
alternate pair order between boxes and actual ToyCar/Avocado models.

Each 34-second run moves its existing IDs through four phases, eight seconds
apart: separate islands, mixed tile membership across those same islands, a
compact grid, and separate islands again. Samples start four seconds into each
phase so presentation, normal shrinking, and cached statistics can settle. The
runner checks IDs/counts, structural events, settled tile counts, GL/submission
errors, and resolution-cap warnings. The last phase continues until shutdown.

GPU estimates describe owned allocations, not dedicated VRAM. These short runs
are layout-specific and depend on headset pose and activity; they do not establish
hardware-wide throughput. No quality or resolution setting differs within a pair.

## Recorded results

Measured on Steam Frame on 2026-10-27 with this implementation, parent revision
`db76506`. [Raw summary](regroup-frame.json) retains all four runs and phases.
Every case retained the same 16 body IDs throughout all movements and had no
resolution-cap warnings, GL errors, or submission/allocation failures.

| Geometry / phase | Split-only FPS | Adaptive FPS | Split-only GPU estimate MiB | Adaptive GPU estimate MiB |
| --- | ---: | ---: | ---: | ---: |
| boxes / mixed | 82.0 | 89.9 | 474.2 | 110.2 |
| boxes / compact | 90.0 | 90.0 | 110.2 | 92.2 |
| models / mixed | 60.9 | 89.8 | 474.2 | 110.2 |
| models / compact | 89.6 | 89.6 | 110.2 | 92.2 |

Adaptive cases followed **split → rebalance → merge → split**, with two body tiles
in the wide/mixed phases and one in the compact phase. Split-only controls kept
two body tiles after their initial split. The final wide phase returned to the
same 110.2 MiB allocation in every case, with approximately 90 FPS. This checks
that memory savings did not depend on retained oversized generations or fewer
objects, and that tiles could split again after merging.

An initial model fixture correctly refused a merge below the saving threshold;
the final compact grid distributes each old tile across the footprint so a
qualifying merge is exercised. One earlier run was invalidated by live controller
input rebuilding bodies. The final automated runs disable those controls and
reject any ID change. Neither the allocation threshold nor the rendering quality
was relaxed to make the cases pass.

## Correctness and lifecycle

- CPU planner tests cover default opt-outs, qualifying compact merges, spatial
  rebalancing, >=20% allocation savings, complete ID coverage, masked acceptance,
  blended rejection, hidden/near-plane fallback, and simulated-clock stability.
- Actual GLES matching-grid output is identical before/after tile reassignment and
  merging at stereo/lateral viewpoints, including external floor/body occlusion.
  Common-plane alignment checks continue to cover different tile origins and
  lateral head motion. Existing material, near-plane, indexing, and crop checks
  remain covered.
- Presentation mocks inject texture/show failures into either output. Both old
  tiles stay retained until all outputs succeed. Merge rollback/retry retains both
  sources; view-culled replacements can retire correctly without showing.
- A removed/hidden pending output transfers its retained chain to its surviving
  sibling. Live removal and surface synchronization share this path. Resource
  statistics include every retained snapshot; shutdown/reload destroy the chains.
- A bounded 26-second native lifecycle run additionally exercised split/rebalance/
  merge, hide/show, half/full body removal, and live boxes-to-models script reload.
  Reload committed and full removal left only the floor. SIGTERM stopped normally.
- Allocation failure occurs before membership changes and keeps the existing
  five-second retry backoff. Already-started handoffs finish even if the policy
  is subsequently disabled.

CPU/API, GLES, presentation mocks, examples, script-lifecycle, grab-button, and
documentation checks passed. The final engine matched the benchmarked executable
byte for byte. No persistent demo remains running.

Headset validation failed: the interactive dynamic-body fixture became laggy,
and pressing the wide/compact bumper was followed by a reported headset crash.
The engine log shows multiple tiles growing to 4096 pixels per eye, repeated
buffer allocation failures, and then SteamVR quitting. This supports memory
pressure as a suspect, without establishing the compositor crash cause.
The automated fixture used kinematic bodies and did not cover this falling-body
path. The interactive fixture now also uses kinematic bodies so idle objects do
not fall off the platform. Subsequent allocation protection and continuous-motion
validation are tracked in [fast-motion validation](fast-motion.md). Headset
acceptance still requires a separate visual run; no persistent demo is running.

After buffer-budget hardening and the stationary interactive-fixture correction,
the user accepted the boxes/models headset visual check on 2026-10-27, including
wide/compact movement, grabbing and head-motion occlusion. Large-count continuous
motion and longer-duration acceptance remain separate outstanding checks.
Automatic grouping
retains existing scene-order and coplanar-depth limitations; fixed groups remain
available where scripts need direct control over compositor surfaces.
