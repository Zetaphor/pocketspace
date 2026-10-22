# Steam Frame overlay engine — agent handoff

Prepared October 22, 2026. This guide supplements [remaining work](remaining-work.md) and the repository's `docs/panel-integration-status.md`, `docs/interaction-status.md`, `docs/input-architecture.md`, and `docs/javascript-api.md`.

## Current: personal eye calibration

File-based head-relative correction and an in-headset generator are implemented;
initial Deckard build/file round-trip checks passed, but Deckard stopped resolving
before the final synchronization and startup checks (see calibration document);
see [eye calibration](eye-calibration.md) for commands, format, validation, and
API behavior. `getEyeTracking()` stays raw; consumers opt into
`getCalibratedEyeTracking()`. Next wearer task: run the nine training plus five
validation targets, then verify a saved correction while turning the head.
Earlier plane-fit experiments and raw-accuracy notes below are historical;
no personal correction has yet passed wearer validation. Further performance
optimizations remain deferred by user direction.

## Current direction: tracking API research

The initial implementation now provides `getEyeTracking()`,
`getEyeFoveationCenters()`, and `getFingerTracking(hand)`. See
[tracking API](tracking-api.md) for exact semantics, null/freshness behavior,
the conservative scene-app gaze policy, regression commands, and logs.
`scripts/tracking-test.js` is the wearer diagnostic; `scripts/tracking-smoke.js`
checks the API without drawing. Live data and reload checks passed at 90 FPS
headlessly. The diagnostic text panel reported 83 FPS. Gaze alignment, individual
finger interpretation, and primary-app coexistence remain wearer checks.
The historical research-only state described below is superseded by this API.

Wearer results: fingers worked with the Spigen cover removed; gaze aligned best
on the right and poorly at the center/left. No gaze correction was applied.
The next diagnostic is `scripts/gaze-accuracy-test.js`, a nine-point trigger-
captured accuracy grid without a live dot. Its synthetic script test passes;
the completed wearer grids are analyzed in the latest follow-up below. See tracking-api.md.

The user deferred further performance work and requested eye/finger tracking
research. See [tracking research](tracking-research.md) for primary documentation,
actual Deckard probes, API proposal, and limitations. Combined OpenVR gaze and
global raw finger capacitance were observed; native skeleton bindings resolve
31 bones but remain inactive/NoData in this overlay probe. WebSocket discovery
advertises gaze and skeleton types without delivering them in ordinary component
updates. The isolated diagnostic is `scripts/tracking-probe.cpp` with manifests
under `cfg/tracking-probe/`; normal engine bindings/API are unchanged. Further
API implementation must preserve primary-app focus and distinguish raw sensors
from reconstructed skeletons. No primary scene app or wearer test was used in
the documented native probes.

## Start here

### Latest selection session (October 22, 2026)

Performance follow-up is documented in [performance measurements](performance.md).
The initial code and measurement notes are committed as `4ee849f` and `f9b238d`.
The follow-up cleanup caches uniform locations, hoists per-eye pose calculations,
reuses the render-ready vector, avoids copying drained input events, and drops
missed frame deadlines instead of accumulating catch-up work.
Deckard now uses Release; model-space ray picking replaces per-triangle world
transforms. The Toy Car runtime check held 90 FPS, and the fixed CPU benchmark
reduced picking time by 32.8%. All bounded performance runs stopped cleanly.
The user explicitly excluded investigation of the Frametop report from scope.
After cleanup commit `bd44825`, a shared per-asset static triangle BVH replaces
the runtime linear scan. Its direct benchmark compares BVH and linear hits;
the four-car scene held 89.9–90 FPS with picking around 0.014 ms/frame.
Rendering/GPU completion now dominates that measured workload. The index costs
about 79 ms and 3.08 MiB for Toy Car, once per loaded asset. See performance.md
for test commands, logs, and limitations; no new wearer confirmation was collected.

Selection and initial performance changes on top of `d7db4b9` were committed as
`4ee849f` and `f9b238d`; the subsequent cleanup is recorded in Git history.
Do not overwrite these changes using the older baseline below. See
[model selection test](model-selection-test.md) for exact logs and observations.
The wearer confirmed the straight-on rear-cube / empty-frame regression fixed.
Fixes remove spherical fallback hits, initialize triangle edges before cross
products, populate normalized model bounds, and prevent native rectangular
object surfaces or stale hover from overriding fresh custom-laser mesh picks.
The final retest reported 90 FPS with `gl_error=0`. Toy Car is excluded after
roughly 35 FPS and a reported Frametop disruption; causation is unconfirmed.
Next: isolate rendering versus picking costs without repeating the car headset
exposure, then complete explicit per-hand selection regression coverage.

- Sigma (`192.168.50.240`, user `zetaphor`) holds the committed source at `/home/zetaphor/Code/sf-overlay-mvp`. Its Git remote is `https://github.com/Zetaphor/sf-overlay-engine.git`, branch `main`.
- Deckard is the SteamVR build and headset runtime host at `/home/steamos/Code/sf-overlay-mvp`. Reach it from Sigma with `ssh deckard`.
- At handoff, Sigma, Deckard, and `origin/main` were aligned at `efc825d726bf6f734fa72e9087bdebe0cbf98a43`; both working trees were clean, and no overlay engine was running. Verify those facts again before editing or starting a test.
- From this WSL environment, the existing key works with `ssh -i ~/.ssh/id_ed25519 zetaphor@192.168.50.240`. Sigma's login shell is fish; use `bash -lc '...'` for Bash-specific commands.

```sh
ssh -i ~/.ssh/id_ed25519 zetaphor@192.168.50.240 'hostname; cd ~/Code/sf-overlay-mvp && git status --short && git rev-parse --short HEAD'
ssh -i ~/.ssh/id_ed25519 zetaphor@192.168.50.240 'ssh deckard "hostname; cd ~/Code/sf-overlay-mvp && git status --short && git rev-parse --short HEAD && pgrep -af sf-overlay-engine || true"'
```

The historical DeepSeek Harness handoff is session `session-ccca95f8-9c7a-4acf-8a91-8a5d2b8044fb`, stored on Sigma at `~/.dsh/sessions/--home-zetaphor-llm-wiki--/session-ccca95f8-9c7a-4acf-8a91-8a5d2b8044fb/session.v4.jsonl.zstd`. The current repository documents the later work; read the historical session only when its earlier design context is needed.

## Verified behavior

- The wearer confirmed readable panels, pointer clicks with pixel coordinates, world/viewer-facing/head-locked/arbitrary transforms, clean script reload, avocado manipulation, and live raw controls from both hands.
- The wearer confirmed independent custom-laser grabs of an avocado and a box, simultaneous distinct held objects, beams shortening at the targets and staying at their grabbed lengths, depth push/pull adjustment, and no drop during fast hand movement until trigger release. These were headset observations, not just logs.
- The avocado/box test ran near 90 FPS with `gl_error=0`. The two-hand probe logs distinct `overlay.grabStart` IDs and `SIMULTANEOUS_GRABS`. A short rerun after the last source change loaded the scene, reported both controller poses, and exited cleanly.
- Scope of that verification is the tested headset session and scenes. Exact selection on complex or concave meshes and performance with high-triangle assets remain open.

## Code map

| Area | Location | What it owns |
| --- | --- | --- |
| Scene, overlays, render loop, mesh picking, per-hand grabs, visible laser length | `src/main.cpp` | SteamVR integration and QuickJS object/event bindings |
| Global Steam Frame component stream | `src/frame_input.cpp`, `src/frame_input.h` | Local `vrserver` WebSocket, raw state, callback and queue events |
| Controller actions | `src/controller_input.cpp`, `src/controller_input.h` | Focus-sensitive SteamVR action mapping |
| Models and asset bounds | `src/model_asset.cpp`, `src/model_asset.h` | GLB loading and GPU asset upload |
| 2D panels | `src/panel.cpp`, `src/panel.h` | Canvas drawing, text, image, and panel texture lifecycle |
| Headset regression scenes | `scripts/two-hand-grab-test.js`, `scripts/panel-test-suite.js` | Two-hand object behavior and panel/input behavior |
| Independent raw-input comparison | `scripts/steam-frame-input.py` | Diagnoses SteamVR input changes without the engine |

The input path is pose → mesh triangle hit (with a small bounds fallback) → per-hand hover/trigger → grab state. The grabbed beam length is fixed at acquisition; only depth adjustment changes it before release. Native SteamVR pointer events remain available for panels and as a legacy object-grab path.

## Build and headset checks

Build and run on Deckard:

```sh
cd ~/Code/sf-overlay-mvp
cmake --build build -j2
timeout -s TERM -k 5 180 ./run-overlay-engine.sh scripts/two-hand-grab-test.js
timeout -s TERM -k 5 180 ./run-overlay-engine.sh scripts/panel-test-suite.js
```

Use one test scene at a time. Redirect output to a named `/tmp/` log when running in the background, record the PID, and stop that PID with `kill -TERM`; verify `completed frames=...` and that `pgrep -af sf-overlay-engine` finds no engine afterward. The two tests require a wearer for interaction checks. Inspect FPS, `gl_error`, grab/hover events, and the wearer's observations separately. A build, OpenVR success return, or event log does not prove what the wearer sees.

## Remaining work and constraints

Read [remaining work](remaining-work.md) for prioritized work and completion checks. The immediate engineering items are complex/concave model selection, profiling high-triangle picking versus rendering, experimental laser pitch, and a dedicated grip pickup gesture. The Toy Car probe used a 108,936-triangle asset and ran near 31 FPS; the cause was not isolated. Do not present that slowdown as a proven picking bottleneck.

The `vrserver` input WebSocket is undocumented. If controller events stop after a SteamVR update, compare `src/frame_input.cpp` with `scripts/steam-frame-input.py` before changing action mappings. The documented SteamVR action API did not provide complete global button input in the tested overlay context.

Live calls to `SetOverlayRaw` returned `VROverlayError_RequestFailed` (`23`) in this environment; dynamic texture/bounds experiments also failed to produce a reliable visible cutoff. The working laser shortens its overlay geometry with `SetOverlayWidthInMeters` and shifts its center. Preserve the grab-length lock when changing that renderer. Earlier panel submission passed a `GLuint` address rather than the texture handle and stalled the first frame; current panel submission uses the handle.

## Change workflow

1. Inspect `git status` on Sigma and Deckard before copying or resetting files. Sigma is the source checkout; Deckard is the runtime copy.
2. Make and review source changes on Sigma, copy only the intended files to Deckard, then build and run a bounded headset test. Keep verified behavior distinct from code/log-only results.
3. Commit and push from Sigma when the change is accepted. Deckard may not have GitHub credentials; the prior workflow transferred a Git bundle from Sigma, fetched it on Deckard, and aligned Deckard's checkout after confirming its only local changes were the files just copied. Never discard unknown Deckard work.
4. Update the relevant status/API document and [remaining work](remaining-work.md) with the exact revision, test scene, outcome, and any unverified gap.

## Latest gaze accuracy follow-up

Two grids were completed in `/tmp/gaze-accuracy-c74481b.log`; stable median
errors were 20.14 and 18.71 degrees with a large downward bias. Five native
panel-coordinate comparisons passed (maximum 2.98e-8 m difference). No raw gaze
correction was applied: affine fits across the separate grids still missed
many targets. The capture script now retries unstable points (>50-pixel 95th-
percentile spread). Offline analysis and synthetic tests are available in
`scripts/analyze-gaze-accuracy.py` and `tests/gaze-analysis-test.py`; the native
geometry diagnostic is `scripts/panel-ray-check.cpp`. Details and caveats are
in tracking-api.md. Next: explicit head-relative per-user calibration and
held-out validation, preserving raw gaze as a separate signal.
