# Multi-script implementation guide

Status: complete. Every step below was implemented and verified; the [approved design](multi-script-plan.md) defines the behavior.

## How to use this guide

Work from Step 0 through Step 12, with Step 7a between Steps 7 and 8. Finish a step's exit gate before starting the next. Keep each step in a focused commit, update this checklist with the commit and evidence, and never claim a headset result from a build or offscreen test. Preserve unrelated work. If a step exposes a conflict with the approved behavior, record it in the plan and resolve it before changing the contract.

Begin with Step 0. At the end of each step, report the changed files, exact checks and outcomes, and any evidence still needed. Use the next step's checklist as the next work item; do not start several broad steps in one change.

Work in `/home/steamos/Code/pocketspace` on **Deckard**: edit, build the ARM64 engine, and run SteamVR/headset checks there. Do not overwrite uncommitted work. On-device noninteractive tests must not capture pointer input. Check for an existing demo before launching; stop the test demo and verify exit when finished. See [AGENTS.md](../AGENTS.md) for the full workflow.

The Step 0 baseline used the headless build and the narrow tests:

```sh
cmake -S . -B build-headless -DSF_BUILD_ENGINE=OFF
cmake --build build-headless --target runtime-stats-regression scene-lookup-regression lifecycle-input-regression physics-regression -j1
./build-headless/runtime-stats-regression
./build-headless/scene-lookup-regression
./build-headless/lifecycle-input-regression
./build-headless/physics-regression
python3 tools/lint-docs.py
npm ci && npm run docs:build
```

The headless tree cannot prove the full engine links or renders. For engine changes, use `cmake -S . -B build` followed by `cmake --build build -j1`. Build only the relevant excluded regression targets as needed. Do not run an interactive headset scene until the headset is available for validation.

At every code step: run focused regressions, `python3 tools/lint-docs.py` when API/docs/examples change, and `git diff --check`. Run `npm run docs:build` for site content or navigation changes. Build new CMake targets after reconfiguring the appropriate build tree. Do not add tests that only repeat the implementation; test failure, cleanup, and competing-instance cases.

Record each completed step in a small table here with commit, headless and documentation checks, Deckard build/checks where required, and any outstanding headset observation. An unchecked step is not complete. The terminal state is a clean, pushed branch with the whole approved behavior verified.

## Step 0 — Baseline and ownership inventory

- [x] Confirm the work starts at a clean commit (`c5e5369`). Record the current one-script CLI behavior and baseline headless regressions.
- [x] Inventory process-global `Scene` pointers, `JSContext`/callback state, `frame_input::Monitor` queues, controller snapshots, hover/grab/pointer state, surface and panel overlay maps/keys, render caches, shadow state, statistics, and `scenePersistence::directory`. The findings are in the [ownership inventory](multi-script-ownership.md).
- [x] Classify engine-shared and per-instance state, identify local-ID collisions and the lifecycle paths that must remain isolated in the ownership inventory.
- [x] Add [left](../tests/scenes/multi-script-left.js) and [right](../tests/scenes/multi-script-right.js) fixtures with object and panel ID `1` and the same top-level names. Both pass JavaScript syntax checks; concurrent runtime loading waits for Step 1.
- [x] Record a representative one-script Deckard FPS/memory baseline with an active headset and noninteractive overlay. Step 2 contains the matched one-scene and two-scene sample and its limits.

**Exit gate:** the ownership map covers creation, candidate reload, successful reload, failed reload, manual stop, callback error, and process shutdown. Baseline headless checks pass without changing behavior; the one-script device measurement is recorded or explicitly tracked for completion before Step 2 performance comparison. No interactive headset test is needed yet.

## Step 1 — Instance registry and script lifecycle

- [x] Introduce a headless registry in `src/script_registry.h` with stable positive IDs and reserved scratch ID `0`. Each loaded user script has a separate QuickJS runtime/context and scene. Wire the registry into the production frame loop in Step 2; initialize the scratch context in Step 8.
- [x] Scope `currentScene`, `liveScene`, and the scoped input-callback globals around each registry load/reload/update/input call. Store callback ownership per `ScriptHost` and restore the prior scope after calls and exceptions. Event-queue fanout arrived in Step 2.
- [x] Add launch, reload, manual stop, and update/input-callback error-stop transitions. Reject a second live instance of the same canonical file. Successful reload replaces only that host; failed reload preserves its context and callback. Structured control-protocol errors arrive in Step 7; bus subscriptions arrive in Step 5.
- [x] Use catalog launch for one or many startup scripts. The registry supports independent automatic file-change reload. Empty startup is the production default; scratch QuickJS initialization arrives in Step 8.
- [x] Add a focused `multi-script-lifecycle-regression` using real QuickJS bindings: two colliding local ID sets and callbacks, duplicate rejection, automatic file-change reload rollback, scoped manual stop, and update/input-callback error isolation. The existing single-script lifecycle and examples regressions also pass.

**Scope at completion:** Step 1 proved the multi-host lifecycle without VR. Step 2 integrated the registry, native owners, and event queues into the frame loop. Scratch gets its live context in Step 8.

**Exit gate:** two scripts can be loaded and stopped independently in a headless harness; one-script regressions still pass. No renderer or live headset claim yet.

## Step 2 — Shared frame, rendering, and resource cleanup

**Complete.** The engine starts with no user scripts. Catalog `--launch` entries create independent QuickJS scenes; the shared frame visits a stable snapshot of running instances and binds each pass to its own render state, native surfaces, panels, event queue, interaction state, shadows, and model resources. Object and panel IDs remain local to their script; native overlay keys include the instance ID. The physical input monitor and controller actions are polled once per frame. The engine uses one OpenVR session, EGL context, compositor loop, and startup anchor. Scratch ID `0` is reserved but allocates no placeholder scene resources; its QuickJS scene API is Step 8.

- [x] Render each running instance in stable order with per-instance stereo surfaces, physics, asset ownership, and render caches. Two scripts with overlapping object and panel ID `1` appeared together in the headset on 2026-11-01.
- [x] Support zero-user-script frames and catalog-only startup. A bounded empty Deckard run completed 338 frames; no default example or positional-script compatibility path remains.
- [x] Scope successful reload, failed reload, manual stop, and error stop to their owner. A failed candidate retains the old scene. A production stop hook releases the owner's surfaces, panels, and render state before QuickJS teardown; control requests will call the registry stop operation in Step 7.
- [x] Keep one physical poll and fan out raw events to distinct script queues. The physical monitor no longer stores a scene-local hover ID; synthetic event copies clear their object IDs defensively. Cross-script hit arbitration followed in Step 3.
- [x] Verify two offscreen scenes with overlapping numeric IDs, distinct pixels, and three independent GPU allocation/cleanup cycles. The surviving scene renders identical pixels after the other is destroyed.

**Deckard evidence:** The two-scene bounded run reached frame 900 at 89.5 FPS with two surfaces, two ready surfaces, and GL error zero. The user confirmed both boxes were visible. A second scene's injected `update()` exception stopped only instance 2; instance 1 reached frame 900 at 89.4 FPS with one surface and GL error zero. Two edits to the left script produced three left loads, one right load, and two instance-1 resets while both surfaces remained. Each bounded engine run exited with grouped buffer usage zero and no Pocketspace process left. Headless regressions passed for lifecycle, input, native keys/resources, scene containers, panel submission, asset lifetime, GLES occlusion, and the new two-scene render test.

**Matched device sample:** With the same noninteractive Box fixture and an awake headset, one scene reported 89.4 FPS, 68,100 KiB RSS, and 6.7% process CPU; two scenes reported 89.2 FPS, 68,272 KiB RSS, and 9.8% process CPU. FPS is capped near 90. RSS and CPU are single `ps` samples five seconds into separate 11-second runs, not long-run performance estimates.

**Exit gate:** passed for two-scene rendering, local-ID isolation, bounded resource cleanup, engine idle mode, and user-observed headset appearance. External control and live manual-stop commands are Step 7; scratch rendering is Step 8. Per-script input arbitration begins in Step 3.

## Step 3 — Input arbitration and event routing

**Complete:** A shared model-ray target is selected once per hand from all running scenes by closest hit, then stable instance ID. The picker scopes each scene while testing its objects, including skinned models that prepare scene-local geometry. A held object keeps its owner. Only the winning scene receives mesh hover and laser-triggered grab acquisition; existing grabs still process release. A per-hand frame owner blocks a second scene from acquiring a held or newly acquired controller through native overlay input. Shared laser overlays update once per frame after all scenes run, using the selected hit distance and the union of running scenes' visibility requests. Native SteamVR pointer events retain their overlay owner; SteamVR determines which panel receives a pointer hit. Cross-script panel hover transitions end the prior owner's hover even if SteamVR omits its focus-leave event; stale focus-leave events cannot clear a new owner. The physical controller monitor has no scene-local hover ID. Disconnect clears the affected hand's copied events from every running script queue.

The pure comparator regression covers closest hit, stable ties, order independence, and invalid distances. The native grab harness verifies that two scenes with local object ID `1` cannot both acquire a laser-triggered hold. Device runners now build catalog launch arguments instead of passing positional scene paths, and their process checks use the current engine name.

**Headset evidence (2026-11-01):** The user confirmed both overlapping boxes and both panels worked, with one target reacting to each action. A bounded Deckard run logged grabs from both scripts and both hands, a panel click, 90.0 FPS with two scenes/surfaces, GL error zero, and zero grouped buffer usage at exit. In a separate panel-depth run, the user confirmed a click at the overlap affected only the closer blue panel and simultaneous two-hand box grabs moved independently. The log recorded seven blue-panel clicks and overlapping near/far script grabs; the run reached frame 6300 at 90.0 FPS with GL error zero and exited at frame 6690 with grouped buffer usage zero. No run left an engine process.

**Step 3 headset closeout (2026-11-01):** With both scripts running, the user turned one controller off and on; the connected hand kept grabbing and the reconnected hand resumed grabbing. While both boxes were held, touching only the blue fixture triggered its automatic reload. The user confirmed blue reset, orange stayed held and movable, and both boxes could be grabbed again after release. The engine logged instance 1 scene reset, two scenes and surfaces at 90.0 FPS with GL error zero, completed 11,895 frames, and exited with grouped buffer usage zero. No Pocketspace process remained.

**Automated closeout checks:** The multi-script lifecycle regression now verifies that disconnect clears only the affected hand's queued events, reconnect resumes delivery to both scenes, stopping a held owner releases only its native resources and interaction state, and a callback can remove its own object and panel without changing the other scene. The production frame loop clears each script's affected hand queue on controller disconnect. On Deckard, the engine and focused regression targets build; `multi-script-lifecycle-regression`, `input-arbitration-regression`, `lifecycle-input-regression`, `scene-container-regression`, and `panel-submission-regression` pass. The headless `lifecycle-input-regression` and grab-button regression pass.

- [x] Sample tracking and raw controller input once per frame. Fan out independent event queues and dispatch callbacks only to their owning instance.
- [x] Arbitrate model targets across instances by closest hit and stable instance ID for equal distances. Retain a held object's owner; route native panel pointer events to their overlay owner.
- [x] Scope hover, grab, panel pointer, and laser state by instance, including cross-script panel hover transitions.
- [x] Verify overlapping objects and panels, equal-distance model tie logic, and simultaneous two-hand grabs. The headset observations above cover overlap and both hands; the comparator regression covers ties.
- [x] Verify disconnect/reconnect queue routing, stop while held, callback-triggered mutation, and unaffected-scene input state after one script stops/reloads in focused regressions. Run `lifecycle-input-regression` and the relevant grabbing/panel checks.

**Exit gate:** passed. Exactly one owner receives each interaction, stopping one script leaves another's input state intact, and disconnect/reconnect resumes per-script delivery. Automated lifecycle checks and headset acceptance of overlapping targets and two-hand grabbing pass. No engine process was left running by these checks.

## Step 4 — Saved scene namespaces

**Complete:** `ScriptScope` selects the calling host's namespace, and reload preserves it. The directory name percent-encodes the script's canonical catalog-relative path into one path component; scratch uses `scratch/`. `importSceneContainer(sourceScript, name)` resolves its source through the catalog, then uses the normal file read and staged restore. Scratch saves are not an import source, because the approved import contract names catalog scripts and scratch has no live context until Step 8.

**Automated evidence:** On Deckard, `scene-persistence-regression` runs two real QuickJS instances through same-name isolation, import in both directions, a symlink alias, nine rejected sources, six rejected or malformed save names, reservation failure rollback, symlinked namespace refusal, successful and failed reload, import from a stopped source, restart with swapped instance IDs, and scratch. Removing namespace preservation, the symlink check, or the import source each made it fail. The ARM64 engine builds; `examples`, `multi-script-lifecycle`, `lifecycle-script`, `lifecycle-input`, `scene-container`, `asset-lifetime`, `point-light`, `spotlight`, and `directional-light` regressions pass. A bounded production-engine run with objectless scripts (`--scripts-dir`, nested `--launch`, `--scene-state-dir`) wrote `first.js/layout.json` and `tools%2Fsecond.js/layout.json`, imported across scripts, rejected traversal, and left no process. The save root and format string then moved to `pocketspace`. Pre-release compatibility was then removed entirely: snapshots accept only version 1, and compatibility-only APIs and `sf_overlay_engine` identifiers are gone.

**Controller ray change (2026-11-01):** Step 4's headset run exposed two input defects from Step 3. The model picker treated a miss (`FLT_MAX`) as a valid target, so grabbing with nothing under the ray selected the lowest-ID box. Panels also used a separate SteamVR pointer, so the drawn laser passed through them. The engine laser is now the only panel pointer, with one closest-hit ray per hand; `input-arbitration-regression` covers the miss, panel selection, pixel mapping, hover and press capture. The user confirmed in the headset that the drawn laser clicks panels, the overlap clicks only blue, panel clicks and empty-space presses grab nothing, both boxes grab with trigger and grip, and two boxes move independently. That run reached 90.0 FPS with two scenes and GL error zero, logged no script stops, and exited with grouped buffer usage zero and no process left. Model surfaces remain native SteamVR overlays.

**Final checks:** Headless: `native-resource-key`, `native-scene-resources`, `runtime-stats`, `scene-lookup`, `lifecycle-input`, `physics`, `physics-collision`, and `physics-joint` regressions, `grab-buttons-regression.py`, `picking-regression.py`, docs lint, and the site build pass. On Deckard: the ARM64 engine and `input-arbitration`, `scene-persistence`, `examples`, `environment`, `spotlight`, `point-light`, `directional-light`, `pbr`, `animation-blend`, `spot-shadow`, `asset-lifetime`, `multi-script-lifecycle`, `lifecycle-script`, `lifecycle-input`, `scene-container`, `panel-submission`, and `native-resource-key` regressions pass. Noninteractive `scene-persistence.py` in all four modes and `container-lifecycle.py` pass, with no script stops or surviving engine process. The remaining engine-based CMake targets were not rebuilt against this code; Step 11 runs the full set.

- [x] Scope `saveSceneContainer`/`loadSceneContainer` under a stable key derived from the script's canonical catalog path; use `scratch/` for instance `0`. Retain `--scene-state-dir` as the root and current save-name validation.
- [x] Add explicit cross-script import from a stable catalog key. Read detached snapshot data and restore through the existing staged path with fresh IDs. Reject traversal, symlink escapes, malformed content, and missing source files.
- [x] Remove support for saves from before namespaces: the default root and format string changed to `pocketspace`, with no migration tool.
- [x] Extend `scene-persistence-regression` for same-name isolation, restart, scratch namespace, import, and failed restore without modifying either scene.

**Exit gate:** two scripts can each save `layout` without collision and explicitly import one another's save. Existing snapshot regressions pass.

## Step 5 — Event bus

- [x] Implement `publish`, `subscribe`, and `unsubscribe` with per-instance subscription ownership. Script topics use `space.*`; only the engine publishes `engine.*`, which scripts may subscribe to. `subscribe("*")` receives events from other instances and engine events, never the subscriber's own publications; an exact-topic subscription may receive its own publication.
- [x] Copy bounded JSON-compatible payloads between runtimes. Reject unsupported/cyclic/oversized values. Defer dispatch until after the publisher callback; preserve publish order and stable subscription order. Limit per-frame work and queued bytes, count drops, and expose overflow to the publisher.
- [x] On reload, atomically replace subscriptions only after the candidate commits. On stop, remove subscriptions and undelivered events. Do not replay events to later-started scripts. Scratch uses source ID `0`; engine events use null source ID.
- [x] Add a focused bus regression covering exact/catch-all/self rules, multiple subscribers, chained publishing, loops/overflow, failed reload, stop during delivery, and one subscriber throwing while others continue.

**Complete.** `src/event_bus.h` holds the topic rules, payload validation and copying, per-source accounting, and a resumable delivery cursor. `ScriptRegistry::dispatchBus` runs one budgeted turn per frame after every scene's callbacks and update. Subscriptions and a loading candidate's publications live in the candidate's `Scene`, so commit replaces them atomically and failure discards them. Decisions made for this step: topics up to 128 bytes, payloads up to 64 KiB and 32 levels deep, 256 undelivered messages per publisher, 4 MiB in total, and 1024 deliveries per frame. `publish` returns `null` on overflow and throws on invalid input. The engine publishes `engine.scriptStarted`, `engine.scriptReloaded` and `engine.scriptStopped`. Scratch participates as instance `0` on the bus; its persistent context and reset arrive with Step 8, so the regression loads a script into instance `0` directly.

**Exit gate:** bus tests pass with two real QuickJS contexts and scratch, including cleanup under failure. Public JS API documentation and one small example are updated.

## Step 6 — Catalog, settings, autostart, and recovery

- [x] Add a configurable user scripts root separate from repository examples. Recursively list readable `.js` files under that root; canonicalize and reject traversal and symlink escapes. Use stable catalog keys for launch, autostart, and saved scenes.
- [x] Put the autostart list, global automatic-restart flag, and engine-autostart setting in a validated configuration file. Dashboard and CLI changes write this one file atomically. Reload valid external edits and retain the last good settings on an invalid edit.
- [x] Start no user script by default. Launch configured autostarts independently. Keep manually stopped scripts down for the current session. On callback error, notify, stop that instance, and leave it stopped by default; if global restart is enabled, apply bounded backoff and a retry ceiling. Scratch callback errors reset only scratch.
- [x] Test duplicate and invalid catalog entries, recursive discovery, config edits while running, autostart failure isolation, manual-stop override, crash-loop pacing, and restart-disabled default.

**Complete.** `listCatalogScripts` and `catalogKey` in `src/script_catalog.h` give recursive discovery with stable keys. `src/engine_settings.h` validates, serializes, atomically writes and watches `$HOME/.config/pocketspace/settings.json` (`--settings` overrides it); the CLI and dashboard will use the same writer. `launchAutostart` and the registry's restart policy live in `src/script_registry.h`, with an injectable clock for tests. Decisions made for this step: `--launch` stays as a one-session launch that runs before autostart; live `autostart` edits apply at the next engine start, while `autoRestart` applies at once; restarts wait 1, 2, 4, 8 and 16 seconds, with attempts reset after 60 healthy seconds; callback-error stops show a SteamVR notification from a hidden source overlay. Scratch is excluded from restarts; its reset arrives with Step 8.

**Exit gate:** config, lifecycle, and catalog regressions pass. Restart policy never spins; one failing autostart does not prevent a healthy one.

## Step 7 — Local control protocol and supervised engine start

- [x] Implement a versioned Unix-domain socket under a same-user runtime directory. Define request IDs, instance generation, status/error schema, bounded messages, and a bounded recent-outcome lookup for uncertain mutating requests. Queue mutations/evaluation onto the host thread.
- [x] Expose catalog/list/status/start/stop/reload/settings/stats/watch/publish/eval/scratch-reset operations. Reject stale instance generations and malformed requests. Keep socket operations responsive when no user script runs.
- [x] Add a supervised user service for the engine, with autostart alongside SteamVR optional and off by default. Make offline Start Engine possible through the service manager, independent of the engine socket, when SteamVR is already running. Neither the dashboard nor CLI start command may launch SteamVR implicitly.
- [x] Test concurrent clients, disconnect during a command, duplicate request IDs, timeout with later outcome lookup, service restart, socket permissions, and engine shutdown cleanup.

**Complete.** `src/control_server.h` is the socket transport, polled from the host loop so requests run between frames; `src/control_service.h` holds the operations, request-ID outcomes, generation checks and watch events. The protocol is documented in [control protocol](control-protocol.md). Decisions made for this step: line-delimited version 1 JSON; the persistent scratch context and bounded `eval` (moved forward from Step 8) so `eval` and `scratch.reset` work now; an `eval` timeout stops only that scene. SteamVR is run by the Frame's OS, so the units need no SteamVR guards: `pocketspace.service` runs the engine and `pocketspace-autostart.service`, wanted by `steamvr.service`, starts it only when `engineAutostart` is true. Settings and units always live under `$HOME/.config`, independent of a shell's `XDG_CONFIG_HOME`.

**Exit gate:** a test client can manage two scripts through the socket without racing frame mutation; the offline start path works on Deckard without starting SteamVR implicitly.

## Step 7a — Remove unshipped experiments

Keep in the repository only what production uses now. Experiments stay available in Git history, and the documentation points to the commit that removed them, so nothing is lost if one is picked up again. This also removes the extra compile time they cost.

- [x] Inventory every experiment, prototype, demo, diagnostic and profiling target in `CMakeLists.txt`, including the experiment regressions, `*-experiment`, `*-demo`, `*-pixels`, `*-profile`, `*-prototype` and benchmark targets. Mark each as production or not, and confirm the list with the user before deleting anything.
- [x] Remove the unshipped ones: their targets, test sources and scenes, the `SF_*_EXPERIMENT` and validation macros and every code path they guard in `src/`, the `tests/*-experiment.h` headers that `src/` includes, `SF_RENDER_UNITY`, profiling scripts and runners that only serve them, and the `--experiments` option in `tools/run-regressions.py`.
- [x] Keep coverage of shipped features. Where an experiment regression also checks shipped behavior, such as directional lights, spot shadows, sheen, GPU skinning, PBR depth, PBR LOD or PBR indexing, either move that check onto the shared test objects without experiment macros or ask the user whether to drop it.
- [x] Keep measurement reports in `tests/results/`, but mark each removed experiment's report with the removal commit and how to recover the code (`git show <commit>^:<path>`). Update `docs/architecture.md`, `docs/optimizations.md`, `tests/index.md`, `tests/README.md` and `AGENTS.md` (which then no longer needs its `--experiments` rule) so they describe only what remains, with links to the removal commit where they mention a removed experiment.
- [x] Record full regression build and run times before and after.

**Complete** in `92f4137`. The confirmed inventory removed the `gpu-sync-experiment` bundle (alternate synchronization, static/ordinary vertex shaders, opaque specialization, primitive/cluster visibility, multipart instancing, grouping tie-break, test LOD ratio), the VAO and uniform-cache experiments, shared depth, PBR light-invariant/transform/variant experiments, the PBR depth/LOD demos, and the rigid-batch, rigid-pose, GPU-skinning and animation-profile A/B engines, with their tests, headers, runners and profiling scenes. Shipped-feature checks (directional lights, light and cone selection, spot shadows, sheen, PBR depth/LOD/indexing, GPU skinning, rigid batches, instancing) moved onto the shared test objects. Their fault injection, counters and reference light loop sit behind `SF_TEST_HOOKS`, which only test targets define. The cheap profilers stay: `pbr-cost-profile`, `render-stress-benchmark`, physics performance, `animation-profile-regression`, the memory sampler, `--benchmark-picking`, and the runners that drive the production engine. Reports in `tests/results/` name the removal commit; [the test index](../tests/index.md#removed-experiments) explains recovery.

Clean full check on Deckard with one compile job: before, `run-regressions.py --experiments` took 1377 s (1030 s build, 347 s tests); after, `run-regressions.py` takes 742 s (560 s build, 182 s tests), 42/42 passing. The shared test objects stay optimized in the fast tree, because `pbr-index-production-regression` needs optimized arithmetic for exact pixels; the first after-run caught that.

Follow-up `32839bf` (build time only, no production code change): the GLES regressions link one compiled `tests/render_harness.cpp`, fast-tree regressions reuse one precompiled `src/render_host.h`, the test tree no longer compiles the production engine objects, and CMake uses `ccache` when it is on `PATH`. Clean regression build 560 s → 442 s (full check 616 s, 42/42); a same-tree clean rebuild with a warm cache takes 35 s.

**Exit gate:** no experiment macros or unity builds remain in `src/`; `tools/run-regressions.py` builds and runs every remaining regression in one pass; the production engine and all remaining regressions pass; docs lint and site build pass; and the documentation names the removal commit.

## Step 8 — CLI and scratch REPL

- [x] Implement `pocketspace-cli` commands from the approved plan, including `start-engine` while the engine socket is offline. Make all one-shot commands noninteractive and deterministic with `--json`, stable exit codes, stderr diagnostics, explicit timeouts, and JSONL `watch`.
- [x] Create persistent scratch scene/context at ID `0`. The default REPL evaluates there and can create objects/panels, manage scripts, and publish events. `:enter <id>` evaluates in a selected live script; `:leave` returns to scratch. Targeted evaluation checks instance generation. Script reload/stop returns attached REPLs to scratch; `scratch reset` affects only ID `0`.
- [x] Serialize evaluation from multiple clients; capture result, exception, and `print()` output per request. Bound source/output/runtime and interrupt tight JavaScript loops without stopping other instances. Never auto-retry an evaluation with an unknown outcome.
- [x] Test a real interactive terminal session and a non-TTY harness that parses JSON, watches a bounded stream, checks uncertain outcomes, enters/leaves a script, and edits both scratch and a user scene.

**Complete.** `src/pocketspace_cli.cpp` is a separate C++ executable that links only QuickJS, for JSON, and libedit, for interactive line editing. It does not link the engine. The commands, JSON output, exit codes and REPL are documented in [command line and REPL](cli.md). Scratch's persistent context, bounded `eval` and serialization arrived in Step 7. This step added scratch's `scripts` object, which `src/control_service.h` answers through `handleScratch`. Its `start`, `stop` and `reload` calls are checked immediately, queued, and applied by `applyScratchQueue` between frames, because scratch code can run inside a frame. Their outcome arrives on `engine.*` topics; `engine.scriptReloadFailed` is new for this. Decisions made for this step: a C++ CLI, QuickJS for JSON, libedit for line editing, a `scripts.*` object, and bus events rather than promises for queued results. `start` and `reload` replies already arrive after the load completes, so the CLI has no separate `--wait`. The REPL learns about reloads and stops through a second, watching connection. Every input also carries the entered generation, so it cannot reach a replacement scene.

**Automated evidence:** `control-regression` covers the `scripts` object: queries, checked and queued lifecycle calls (including one from a bus callback), outcomes on the bus, stale queued requests, and that only scratch has the object. `cli-regression` drives the real CLI against the control service without VR, through the checks listed in the [test index](../tests/index.md). It covers the harness path, a bounded watch across an engine restart, and an interactive REPL on a pseudo-terminal. It passed four consecutive runs on Deckard.

**Live evidence (2026-11-04):** The user ran the REPL in a real terminal against the live engine, with a suggested check: create a box in scratch, start and enter `grabbing.js`, return to scratch after editing its file, and use history recall and Ctrl-R. They reported that everything worked.

**Exit gate:** a harness can launch, monitor, publish, evaluate, and stop scripts without parsing human text; an interactive user can create an object in scratch and then enter a script safely. Passed.

## Step 9 — Resource telemetry

- [x] Report engine-wide FPS, CPU, RSS, and DRM GPU memory once. For each script, expose QuickJS heap, object/panel/asset/body counts, measured callback time, and owned allocation estimates where attribution is valid. Expose scratch and event-queue/drop metrics.
- [x] Do not divide process totals among scripts or add shared RAM and GPU totals. Mark unavailable values and sample intervals. Bound telemetry work and cadence so monitoring does not disturb the 90 Hz loop.
- [x] Test metrics for two scripts with different workloads, stop/reload cleanup, unavailable DRM data, and a monitoring client disconnect. Compare telemetry overhead before enabling frequent polling.

**Complete.** `stats` and a once-a-second `resources` watch event report process totals once, then each instance's values; the [resources reference](control-protocol.md#resources) defines them. Decisions made for this step: delivery through `stats` plus watch events built only while a client watches; callback time as milliseconds per second plus the worst frame over the one-second sample; GPU estimates only, split by owner, with lighting environments and the sheen table reported once as shared; nothing new in JavaScript.

QuickJS heap comes from a counting allocator installed on each script's runtime (`src/script_telemetry.h`). It costs one counter update per allocation and nothing to read. `JS_ComputeMemoryUsage` was measured first and rejected for sampling: on Deckard it took 0.009 ms at 1,000 objects, 1.1 ms at 100,000 and 14 ms at 1,000,000, which at the 128 MiB runtime limit would drop a frame every second. The counter also covers runtime-internal allocations, so it reads up to about 80 KiB above QuickJS's own total. Callback time covers update, input and bus callbacks on the owning instance. The engine's GPU estimate is now the sum of `ownedGpuEstimate` per script plus `sharedGpuEstimate`. Each script's own spot-shadow map is counted; before, only the global diagnostic record's map was counted, whichever scene had written it last. The bus also counts queued and dropped publications per publisher.

**Automated evidence:** `control-regression` checks the heap counter against QuickJS's full walk and through an 8 MB allocation and its release; a busy bus callback against idle scratch; null values before the first sample and for stopped scripts; a fresh window after restart; per-publisher bus counters; DRM memory reported as unavailable; and resources events to a watcher that then disconnects. `multi-script-render-regression` checks owned estimates for two workloads, surface depth and retained generations, and a shared environment and sheen table counted once. `cli-regression` reads the per-instance view, the human table and resources events through the CLI. Full check 46/46.

**Device evidence (2026-11-04):** [Telemetry overhead](../tests/results/telemetry-overhead.md) compares four alternating 60-second engine runs on the headset, without and with a watching CLI. With two steady display-only scripts, every run held 90 FPS and CPU averaged 7.9% unwatched and 8.2% watched, within the sampler's one-point resolution. Each watched run received 59 `resources` events. A variable `fast-motion.js` workload swung too much on its own to resolve any difference. Driver GPU memory was unavailable on the device. The engine exited after every run.

**Exit gate:** CLI JSON and dashboard protocol show consistent totals and per-instance values, with estimates clearly labeled. Passed.

## Step 10 — Native SteamVR dashboard

- [x] Build a separate dashboard process modeled on Framecorder's SteamVR-bound user service. It attaches only to an existing SteamVR session and reconnects when the engine starts or restarts.
- [x] Add offline Start Engine, script catalog/launch/stop/reload, autostart/restart settings, error alerts, resource display, and a clear scratch entry. The engine keeps sending the SteamVR notification for callback-error stops, so it appears even when the tab is closed or the dashboard is not running; the tab shows one alert per failing script, counting a restart loop's repeats.
- [x] Refresh at a modest cadence while visible and sleep when hidden. Capture pointer input only while the tab is active. Keep dashboard crash/restart independent of the engine and scripts.
- [x] Test offline/online transitions, two scripts, stopping a held object, script errors, service restarts, and dashboard focus in-headset. Use Framey/Deckloader only as reference; the first release is a native dashboard tab.

**Complete.** `pocketspace-dashboard` is a separate C++ process ([guide](dashboard.md)). Decisions made for this step: C++ in this repository, installed by `tools/install-user-service.sh`; the UI is drawn on the CPU with the panel canvas, which now lives in `src/canvas.cpp` without GL, and uploaded to a GLES texture for `SetOverlayTexture`, because `SetOverlayRaw` flickered on every update in the headset (Framecorder uses a Vulkan texture and logs that its raw fallback may flicker); Stop Engine was added after the first headset run and takes a second press; the engine remains the only sender of SteamVR notifications. The CLI and dashboard share `src/control_client.h`. `src/dashboard.cpp` holds the connection, state, drawing and clicks without OpenVR; `src/dashboard_main.cpp` holds the overlay loop.

As Framecorder does, the dashboard registers a `.vrmanifest` at start and turns its SteamVR auto-launch off, so the systemd unit is the only launcher. Unlike Framecorder, the unit uses `Requisite=` and `PartOf=` rather than `BindsTo=`, because `BindsTo=` implies `Requires=`, and starting the unit by hand would then start SteamVR. The process connects as a `VRApplication_Background` client, which never starts SteamVR.

**Automated evidence:** `dashboard-regression` drives the model against a fake engine on a real Unix socket. `cli-regression`, `panel-submission-regression`, `examples-regression` and `scene-persistence-regression` pass after the client and canvas moves. Full check 47/47.

**Device evidence (2026-11-05):** with `grabbing.js`, `multi-script-left.js` and `crash-on-button.js` in the catalog, the user confirmed in the headset: Start Engine from the offline tab; Stop Engine with its second press; two scenes with live resource values; stopping `grabbing.js` while holding its box; crash notifications with a single counting alert; the tab going offline and reconnecting across an engine service restart; and a scene surviving a dashboard service restart. With the tab closed, the dashboard held no control connection. The first headset run found three faults, now fixed: the `SetOverlayRaw` flicker; a tab already open when the dashboard started stayed blank, because it waited for a show event that never came, so it now checks visibility every loop and draws once at start; and the mouse flip, which the flipped texture bounds made unnecessary. The engine was stopped afterwards and exited.

**Exit gate:** the user confirms the tab's controls, notifications, resource display, and two simultaneous scenes in the headset. Stop the test demo and verify it exited. Passed.

## Step 11 — Full regression and documentation pass

- [x] Update `docs/architecture.md`, `docs/lifecycle.md`, `docs/javascript-api.md`, `docs/scene-persistence.md`, `docs/getting-started.md`, `docs/agent-handoff.md`, the test index, and the smallest useful examples. Keep these pages descriptive of shipped behavior, and remove completed work from `docs/remaining-work.md`.
- [x] Run focused native regressions plus `lifecycle-script-regression`, `lifecycle-input-regression`, `scene-persistence-regression`, `scene-container-regression`, `examples-regression`, physics and render checks affected by ownership changes. Record exact commands and results, not only test names.
- [x] Run documentation lint and site build. Build production ARM64 engine on Deckard with the documented CMake configuration; run a bounded two-script noninteractive soak, then obtain headset acceptance for dashboard, interaction arbitration, and rendering. Keep automated, measured, and user-observed evidence separate.
- [x] Confirm graceful teardown leaves no demo process, leaked overlays, or stale control socket. Check clean Git status, review the diff, commit focused changes, and push when requested.

**Complete.** Documentation: the handoff gained a multiple-scripts section and source-map entries for the registry, catalog, settings, bus, arbitration, telemetry, control and CLI sources; the architecture guide gained a processes section, dashboard identifiers and source-map rows; lifecycle names the tools that manage scripts and the autostart service; the JavaScript statistics wording says scene counts and the 256-object limit are per calling script's scene; the README and site home page mention running several scripts and the dashboard. `scene-persistence.md` and `getting-started.md` already described shipped behavior. The multi-script track is no longer listed in the roadmap.

**Automated evidence (2026-11-05):** `python3 tools/run-regressions.py` built and ran 47/47 regressions in 318 s, including `lifecycle-script-regression`, `lifecycle-input-regression`, `multi-script-lifecycle-regression`, `multi-script-render-regression`, `scene-persistence-regression`, `scene-container-regression`, `examples-regression`, `input-arbitration-regression`, `panel-submission-regression`, `event-bus-regression`, `script-recovery-regression`, `control-regression`, `cli-regression`, `dashboard-regression` and the four physics regressions. `python3 tools/lint-docs.py` passed (27 documents, 83 globals, 47 examples) and `npm run docs:build` built the site. The production tree built from clean with `cmake -S . -B build` and `cmake --build build --clean-first -j1` in 4 min 20 s, with no warnings.

**Measured evidence:** [two-script soak](../tests/results/multi-script-soak.md), 10 minutes with alternating reloads and no input capture: FPS 89.7 to 90.2, mean CPU 9.3%, RSS level at 73.3 to 73.5 MiB after the first few minutes, unchanged heap across reloads, no script failures, clean exit with the socket removed.

**User-observed evidence:** with `tests/scenes` as the catalog, the user launched `multi-script-interaction-near.js`, `multi-script-interaction-far.js` and `pbr-materials.js` from the dashboard and confirmed in the headset: all three scenes rendering together, closest-hit grabs and panel clicks across the two overlapping scripts, a reload while holding releasing only that script's hold, a crash notification with one counting alert, and Stop Engine reporting that a manually started engine did not stop. The engine and dashboard were then stopped: no Pocketspace process remained, the control socket was removed, and SteamVR reported no Pocketspace overlay keys.

**Exit gate:** all approved behavior in the design plan is implemented and verified, with any remaining limitation recorded explicitly. Passed. Recorded limitations: dashboard alerts appear only while its tab is open (the engine's SteamVR notifications cover a closed tab), and the soak is a bounded light workload rather than a long-session result.

## Step 12 — Completion record

Fill in this table as work progresses. Do not mark a step complete from a passing build alone.

| Step | Commit | Headless and docs checks | Deckard checks | Remaining evidence |
| --- | --- | --- | --- | --- |
| 0 | Step 0 ownership-audit commit | Four headless regressions, fixture syntax, docs lint/site build pass | Matched one-scene baseline recorded in Step 2 | None |
| 1 | Step 1 headless-registry commit | Fixture syntax, docs lint/site build pass | ARM64 production/new lifecycle targets build; new, old lifecycle, examples regressions and non-VR model inspect pass | Production frame loop in Step 2; scratch context in Step 8; event routing in Step 3 |
| 2 | Instance-scoped scene traversal, catalog startup, and stop cleanup | Docs lint/site build and diff check pass | ARM64 engine and focused regressions pass; empty startup, two-scene runtime, injected error, and two reloads pass; offscreen overlapping-ID render and user headset visibility pass | External stop command in Step 7; scratch scene in Step 8 |
| 3 | `6b414e6` input lifecycle closeout; headset evidence in this guide | Lifecycle input and grab-button regressions, docs lint/site build, diff check pass | ARM64 engine and focused lifecycle, arbitration, scene-container, and panel-submission regressions pass; user confirmed overlapping targets, two-hand grabs, controller reconnect, and held-object reload isolation | None |
| 4 | Per-script save namespaces, import, and removal of earlier save support | Headless regressions, standalone Python regressions, docs lint/site build pass | ARM64 engine, focused regressions, and noninteractive live persistence/container harnesses pass | Full target set in Step 11 |
| 5 | Event bus commit | Docs lint/site build and diff check pass | ARM64 engine builds; `event-bus-regression` and the full regression set, experiments included, pass | None. Live run with `scripts/event-bus.js` and `tests/scenes/event-bus-peer.js`: lifecycle events, counters, next-frame replies, catch-all self-exclusion, and single delivery after a peer reload, at 88–90 FPS |
| 6 | Catalog, settings, autostart and recovery commit | Docs lint/site build and diff check pass | ARM64 engine builds; `script-recovery-regression` and the full regression set pass | None. Headset run with `tests/scenes/crash-on-button.js` autostarted from a settings file: SteamVR notifications appeared, and five paced restarts brought the panel back before the sixth stop left it down. That run found and fixed a restart that lost the instance identity |
| 7 | Control socket, scratch and user service commits | Docs lint/site build and diff check pass | ARM64 engine builds; `control-regression` and the full regression set pass; `tests/control-live-check.py` managed two scripts in the live engine, a second engine was refused, and `--service` started, restarted and stopped the unit; the autostart unit started the engine only with `engineAutostart` true | None |
| 7a | `92f4137` experiment removal; hash recorded in docs | Docs lint/site build and diff check pass | ARM64 engine builds; clean full check 742 s (was 1377 s with `--experiments`), 42/42 pass | None; no headset behavior changed |
| 8 | CLI, REPL and scratch `scripts` commit | Docs lint/site build and diff check pass | ARM64 engine and CLI build; `control-regression` and `cli-regression` (four repeated runs) and the full regression set, 46/46, pass | None. The user's REPL session in a real terminal against the live engine passed |
| 9 | Per-script telemetry commit | Docs lint/site build and diff check pass | ARM64 engine and CLI build; `control-regression`, `multi-script-render-regression`, `cli-regression` and the full regression set, 46/46, pass | None. Watching showed no measurable FPS or CPU cost on the headset |
| 10 | Dashboard commits | Docs lint/site build and diff check pass | ARM64 engine, CLI and dashboard build; `dashboard-regression` and the full regression set, 47/47, pass; user confirmed the tab's controls, Stop Engine, notifications and alerts, resource display, two simultaneous scenes, a held-object stop and both service restarts | None |
| 11 | Documentation pass and soak commit | Docs lint/site build and diff check pass | Clean production build; full regression set, 47/47; 10-minute two-script soak; user headset acceptance of dashboard, arbitration and rendering; clean teardown | None |
