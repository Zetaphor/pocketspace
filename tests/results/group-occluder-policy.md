# Equal-allocation occluder-aware grouping

Measured on Steam Frame on **2026-10-29**, based on `dae8ca7` plus this isolated
experiment and work instrumentation. **Keep the existing production planner.**
The candidate can improve predicted work in synthetic plans, but its bounded
native moving-model comparison does not establish a useful repeatable gain.

## Candidate

The production planner chooses a minimum-area split from bounded stereo angular
quartile cuts. The test-only tie-break evaluates selected opaque/masked triangle
counts against both eyes' cached child bounds when candidates have the same
minimum allocated pixel area. It prefers fewer predicted external occluder
triangles, retaining deterministic IDs when scores also tie.

The candidate never accepts a larger minimum allocation, lowers sampling density,
changes the 20% area-saving requirement, changes caps/budget/hysteresis, or changes
draw order, shaders and buffer ownership. Blended parts are excluded from the
occluder score. It is also used by the existing rebalancing partition search.
Only the excluded executable reads `SF_GPU_GROUP_POLICY=occluders`; omission or
`pixels` uses the original tie-break. This is not a new script API/default.

## Work instrumentation and rejected comparison

`RENDER_WORK_SAMPLE` counts all-frame submissions by occluder/opaque/blended pass,
including instanced multiplicity, selected LOD indices, actual GPU skin indices,
and complete merged rigid ranges. Scene triangles count selected geometry once
per visible object; the LOD histogram separately records selected levels. These
are submission counts, not visible pixels or hardware invocation counts.

The initial automatic-LOD pair has matching median allocation but changing LOD
histograms and scene geometry during the deterministic movement. Its FPS values
are **rejected as an equivalent-work comparison**. Automatic LOD legitimately
responds to moving projected sizes; allocation matching alone cannot establish
that two timed runs submit the same geometry. The
[rejected summary](group-occluder-policy/dynamic-lod-rejected-frame.json) retains
`matchingGeometry: false` and the observed work data.

The retained comparison explicitly uses `--no-lod`: full-detail geometry in both
variants, mesh-cache optimization and ordered instancing on, 64 moving ToyCars
plus 64 moving Avocados, 768 reference eye pixels and adaptive groups. Models
remain non-interactive; there are no HUD panels, input callbacks or visible
controller beams. This isolates grouping and makes no automatic-LOD performance
claim or quality-reduction claim.

Every retained window has 128 level-0 objects and **7,015,552 scene triangles**.
Each stereo frame submits **14,031,104 opaque member triangles** and 512 member
draws. The Python analysis verifies complete stereo member geometry, stable scene
triangles/LOD counts within each run, and matching counts between variants.
Occluder duplication is allowed to differ intentionally. Missing/nonfinite work,
frame incoherence, object-count changes and mismatched allocations reject a pair.

## Native results

Four 20-second runs reverse pair order in round two. FPS retains 15 samples after
five seconds; GPU and work summaries retain 14 windows after six seconds.

| Pair | Existing FPS | Candidate FPS | Existing GPU ms | Candidate GPU ms | Existing occluder triangles/frame | Candidate occluder triangles/frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 23.62 | 23.98 | 32.21 | 31.66 | 7,517,210 | 7,505,315 |
| 2 | 23.62 | 23.68 | 32.17 | 32.14 | 7,622,971 | 7,613,528 |

These rates come from the instrumented executable, not a normal-engine FPS
acceptance run. Both variants retain 423.03 MiB median estimated GPU allocation and 418 MiB peak
grouped buffers. Occluder counts differ by less than 0.2%; FPS gains are about
1.5% and 0.2%, with an effectively unchanged second GPU pair. This does not
justify promoting the additional predictor into production. Differences in
movement sampling, live head pose, scheduling and unlocked power/thermal state
prevent assigning every small delta to the candidate. These are short bounded
runs, not sustained headset-rate or compositor-latency claims.

[Retained native summary](group-occluder-policy/full-frame.json) includes actual
geometry, completeness guards and allocation matches. Summaries were enriched
from the retained raw work logs with the final completeness check. Raw logs
remain on Frame in `/tmp/sf-group-policy-compare` (rejected LOD pair) and
`/tmp/sf-group-policy-full-compare` (full-detail pairs). All runs exited normally
with expected objects, checked clean native/GL/disjoint/fence status and grouped
budget charges returning to zero.

## Validation and outcome

The native planner regression checks 256 deterministic layouts; 32 select strictly
less predicted occluder work with the same minimum area. All retain complete,
stable IDs, uncapped child density and hidden-member fallback. The Python analysis
checks rejection of malformed/missing samples, frame/count mismatch, LOD drift,
changed geometry and incomplete member draws while accepting intentional
occluder savings.

A separate bounded native scene verifies exact analytic counts for instanced
Boxes, GPU-skinned Foxes, and merged rigid InterpolationTest ranges through the
actual host renderer. It has non-interactive objects, no HUD, no callbacks and no
lasers, clears its actors, and exits through normal cleanup. See the
[validation record](group-occluder-policy/validation-frame.txt).

The normal executable rebuilt byte-identically to the previously validated
normal binary (hash recorded in both validation artifacts); the new work hooks
and planner scoring compile out of that build. Production grouping is unchanged. Retain the experiment for a workload with more
selective equal-area alternatives. Further grouping changes that trade memory
for fewer draws need a separate comparison proving requested sampling density,
explicit allocation/budget accounting and equivalent member geometry; this
experiment supplies no evidence to change thresholds, widen searches, or adopt
shared depth. No new interactive headset acceptance is required or claimed.
