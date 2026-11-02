# Multi-script implementation guide

Status: execution checklist for the [approved design](multi-script-plan.md). The plan defines behavior; this guide defines the order of work. None of the steps below are implemented merely because they appear here.

## How to use this guide

Work from Step 0 through Step 12. Finish a step's exit gate before starting the next. Keep each step in a focused commit, update this checklist with the commit and evidence, and never claim a headset result from a build or offscreen test. Preserve unrelated work. If a step exposes a conflict with the approved behavior, record it in the plan and resolve it before changing the contract.

Begin with Step 0. At the end of each step, report the changed files, exact checks and outcomes, and any evidence still needed. Use the next step's checklist as the next work item; do not start several broad steps in one change.

Use `/home/zetaphor/Code/pocketspace` on **Sigma** for source edits, documentation, and headset-free checks. Sync reviewed commits to `/home/steamos/Code/pocketspace` on **Deckard**; build the ARM64 engine and run SteamVR/headset checks there. Before syncing, compare commit IDs and check both worktrees. Do not overwrite uncommitted Deckard work. On-device noninteractive tests must not capture pointer input. Check for an existing demo before launching; stop the test demo and verify exit when finished. See [AGENTS.md](../AGENTS.md) for the full workflow.

For the first baseline on Sigma, configure the already verified headless build and run the narrow tests:

```sh
cmake -S . -B build-sigma -G Ninja -DSF_BUILD_ENGINE=OFF
cmake --build build-sigma --target runtime-stats-regression scene-lookup-regression lifecycle-input-regression physics-regression -j4
./build-sigma/runtime-stats-regression
./build-sigma/scene-lookup-regression
./build-sigma/lifecycle-input-regression
./build-sigma/physics-regression
python3 tools/lint-docs.py
npm ci && npm run docs:build
```

The headless tree cannot prove the full engine links or renders. For engine changes, sync the reviewed commit and use `cmake -S . -B build` followed by `cmake --build build -j2` on Deckard. Build only the relevant excluded regression targets as needed. Do not run an interactive headset scene until the headset is available for validation.

At every code step: run focused regressions, `python3 tools/lint-docs.py` when API/docs/examples change, and `git diff --check`. Run `npm run docs:build` on Sigma for site content or navigation changes. Build new CMake targets after reconfiguring the appropriate build tree. Do not add tests that only repeat the implementation; test failure, cleanup, and competing-instance cases.

Record each completed step in a small table here with commit, Sigma checks, Deckard build/checks where required, and any outstanding headset observation. An unchecked step is not complete. The terminal state is a clean, pushed branch with the whole approved behavior verified.

## Step 0 — Baseline and ownership inventory

- [x] Confirm Sigma and Deckard start at the same clean commit (`b417ef7`). Record the current one-script CLI behavior and baseline headless regressions.
- [x] Inventory process-global `Scene` pointers, `JSContext`/callback state, `frame_input::Monitor` queues, controller snapshots, hover/grab/pointer state, surface and panel overlay maps/keys, render caches, shadow state, statistics, and `scenePersistence::directory`. The findings are in the [ownership inventory](multi-script-ownership.md).
- [x] Classify engine-shared and per-instance state, identify local-ID collisions and the lifecycle paths that must remain isolated in the ownership inventory.
- [x] Add [left](../tests/scenes/multi-script-left.js) and [right](../tests/scenes/multi-script-right.js) fixtures with object and panel ID `1` and the same top-level names. Both pass JavaScript syntax checks; concurrent runtime loading waits for Step 1.
- [x] Record a representative one-script Deckard FPS/memory baseline with an active headset and noninteractive overlay. Step 2 contains the matched one-scene and two-scene sample and its limits.

**Exit gate:** the ownership map covers creation, candidate reload, successful reload, failed reload, manual stop, callback error, and process shutdown. Baseline headless checks pass without changing behavior; the one-script device measurement is recorded or explicitly tracked for completion before Step 2 performance comparison. No interactive headset test is needed yet.

## Step 1 — Instance registry and script lifecycle

- [x] Introduce a headless registry in `src/script_registry.h` with stable positive IDs and reserved scratch ID `0`. Each loaded user script has a separate QuickJS runtime/context and scene. Wire the registry into the production frame loop in Step 2; initialize the scratch context in Step 8.
- [x] Scope `currentScene`, `liveScene`, and the scoped input-callback globals around each registry load/reload/update/input call. Store callback ownership per `ScriptHost` and restore the prior scope after calls and exceptions. Event-queue fanout arrives in Step 3.
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
- [x] Keep one physical poll and fan out raw events to distinct script queues. The physical monitor no longer stores a scene-local hover ID; synthetic event copies clear their object IDs defensively. Cross-script hit arbitration is Step 3.
- [x] Verify two offscreen scenes with overlapping numeric IDs, distinct pixels, and three independent GPU allocation/cleanup cycles. The surviving scene renders identical pixels after the other is destroyed.

**Deckard evidence:** The two-scene bounded run reached frame 900 at 89.5 FPS with two surfaces, two ready surfaces, and GL error zero. The user confirmed both boxes were visible. A second scene's injected `update()` exception stopped only instance 2; instance 1 reached frame 900 at 89.4 FPS with one surface and GL error zero. Two edits to the left script produced three left loads, one right load, and two instance-1 resets while both surfaces remained. Each bounded engine run exited with grouped buffer usage zero and no Pocketspace process left. Headless regressions passed for lifecycle, input, native keys/resources, scene containers, panel submission, asset lifetime, GLES occlusion, and the new two-scene render test.

**Matched device sample:** With the same noninteractive Box fixture and an awake headset, one scene reported 89.4 FPS, 68,100 KiB RSS, and 6.7% process CPU; two scenes reported 89.2 FPS, 68,272 KiB RSS, and 9.8% process CPU. FPS is capped near 90. RSS and CPU are single `ps` samples five seconds into separate 11-second runs, not long-run performance estimates.

**Exit gate:** passed for two-scene rendering, local-ID isolation, bounded resource cleanup, engine idle mode, and user-observed headset appearance. External control and live manual-stop commands are Step 7; scratch rendering is Step 8. Per-script input arbitration begins in Step 3.

## Step 3 — Input arbitration and event routing

**Complete:** A shared model-ray target is selected once per hand from all running scenes by closest hit, then stable instance ID. The picker scopes each scene while testing its objects, including skinned models that prepare scene-local geometry. A held object keeps its owner. Only the winning scene receives mesh hover and laser-triggered grab acquisition; existing grabs still process release. A per-hand frame owner blocks a second scene from acquiring a held or newly acquired controller through native overlay input. Shared laser overlays update once per frame after all scenes run, using the selected hit distance and the union of running scenes' visibility requests. Native SteamVR pointer events retain their overlay owner; SteamVR determines which panel receives a pointer hit. Cross-script panel hover transitions end the prior owner's hover even if SteamVR omits its focus-leave event; stale focus-leave events cannot clear a new owner. The physical controller monitor has no scene-local hover ID. Disconnect clears the affected hand's copied events from every running script queue.

The pure comparator regression covers closest hit, stable ties, order independence, and invalid distances. The native grab harness verifies that two scenes with local object ID `1` cannot both acquire a laser-triggered hold. Device runners now build catalog launch arguments instead of passing positional scene paths, and their process checks use the current engine name.

**Headset evidence (2026-11-01):** The user confirmed both overlapping boxes and both panels worked, with one target reacting to each action. A bounded Deckard run logged grabs from both scripts and both hands, a panel click, 90.0 FPS with two scenes/surfaces, GL error zero, and zero grouped buffer usage at exit. In a separate panel-depth run, the user confirmed a click at the overlap affected only the closer blue panel and simultaneous two-hand box grabs moved independently. The log recorded seven blue-panel clicks and overlapping near/far script grabs; the run reached frame 6300 at 90.0 FPS with GL error zero and exited at frame 6690 with grouped buffer usage zero. No run left an engine process.

**Automated closeout checks:** The multi-script lifecycle regression now verifies that disconnect clears only the affected hand's queued events, reconnect resumes delivery to both scenes, stopping a held owner releases only its native resources and interaction state, and a callback can remove its own object and panel without changing the other scene. The production frame loop clears each script's affected hand queue on controller disconnect. On Deckard, the engine and focused regression targets build; `multi-script-lifecycle-regression`, `input-arbitration-regression`, `lifecycle-input-regression`, `scene-container-regression`, and `panel-submission-regression` pass. Sigma's `lifecycle-input-regression` and grab-button regression pass.

- [x] Sample tracking and raw controller input once per frame. Fan out independent event queues and dispatch callbacks only to their owning instance.
- [x] Arbitrate model targets across instances by closest hit and stable instance ID for equal distances. Retain a held object's owner; route native panel pointer events to their overlay owner.
- [x] Scope hover, grab, panel pointer, and laser state by instance, including cross-script panel hover transitions.
- [x] Verify overlapping objects and panels, equal-distance model tie logic, and simultaneous two-hand grabs. The headset observations above cover overlap and both hands; the comparator regression covers ties.
- [x] Verify disconnect/reconnect queue routing, stop while held, callback-triggered mutation, and unaffected-scene input state after one script stops/reloads in focused regressions. Run `lifecycle-input-regression` and the relevant grabbing/panel checks.

**Exit gate:** passed. Exactly one owner receives each interaction, stopping one script leaves another's input state intact, and disconnect/reconnect resumes per-script delivery. Automated lifecycle checks and headset acceptance of overlapping targets and two-hand grabbing pass. No engine process was left running by these checks.

## Step 4 — Saved scene namespaces

- [ ] Scope `saveSceneContainer`/`loadSceneContainer` under a stable key derived from the script's canonical catalog path; use `scratch/` for instance `0`. Retain `--scene-state-dir` as the root and current save-name validation.
- [ ] Add explicit cross-script import from a stable catalog key. Read detached snapshot data and restore through the existing staged path with fresh IDs. Reject traversal, symlink escapes, malformed content, and missing source files.
- [ ] Treat legacy shared-directory test saves as disposable; document how to recreate them. Do not add a migration tool.
- [ ] Extend `scene-persistence-regression` for same-name isolation, restart, scratch namespace, import, and failed restore without modifying either scene.

**Exit gate:** two scripts can each save `layout` without collision and explicitly import one another's save. Existing snapshot regressions pass.

## Step 5 — Event bus

- [ ] Implement `publish`, `subscribe`, and `unsubscribe` with per-instance subscription ownership. Script topics use `space.*`; only the engine publishes `engine.*`, which scripts may subscribe to. `subscribe("*")` receives events from other instances and engine events, never the subscriber's own publications; an exact-topic subscription may receive its own publication.
- [ ] Copy bounded JSON-compatible payloads between runtimes. Reject unsupported/cyclic/oversized values. Defer dispatch until after the publisher callback; preserve publish order and stable subscription order. Limit per-frame work and queued bytes, count drops, and expose overflow to the publisher.
- [ ] On reload, atomically replace subscriptions only after the candidate commits. On stop, remove subscriptions and undelivered events. Do not replay events to later-started scripts. Scratch uses source ID `0`; engine events use null source ID.
- [ ] Add a focused bus regression covering exact/catch-all/self rules, multiple subscribers, chained publishing, loops/overflow, failed reload, stop during delivery, and one subscriber throwing while others continue.

**Exit gate:** bus tests pass with two real QuickJS contexts and scratch, including cleanup under failure. Public JS API documentation and one small example are updated.

## Step 6 — Catalog, settings, autostart, and recovery

- [ ] Add a configurable user scripts root separate from repository examples. Recursively list readable `.js` files under that root; canonicalize and reject traversal and symlink escapes. Use stable catalog keys for launch, autostart, and saved scenes.
- [ ] Put the autostart list, global automatic-restart flag, and engine-autostart setting in a validated configuration file. Dashboard and CLI changes write this one file atomically. Reload valid external edits and retain the last good settings on an invalid edit.
- [ ] Start no user script by default. Launch configured autostarts independently. Keep manually stopped scripts down for the current session. On callback error, notify, stop that instance, and leave it stopped by default; if global restart is enabled, apply bounded backoff and a retry ceiling. Scratch callback errors reset only scratch.
- [ ] Test duplicate and invalid catalog entries, recursive discovery, config edits while running, autostart failure isolation, manual-stop override, crash-loop pacing, and restart-disabled default.

**Exit gate:** config, lifecycle, and catalog regressions pass. Restart policy never spins; one failing autostart does not prevent a healthy one.

## Step 7 — Local control protocol and supervised engine start

- [ ] Implement a versioned Unix-domain socket under a same-user runtime directory. Define request IDs, instance generation, status/error schema, bounded messages, and a bounded recent-outcome lookup for uncertain mutating requests. Queue mutations/evaluation onto the host thread.
- [ ] Expose catalog/list/status/start/stop/reload/settings/stats/watch/publish/eval/scratch-reset operations. Reject stale instance generations and malformed requests. Keep socket operations responsive when no user script runs.
- [ ] Add a supervised user service for the engine, with autostart alongside SteamVR optional and off by default. Make offline Start Engine possible through the service manager, independent of the engine socket, when SteamVR is already running. Neither the dashboard nor CLI start command may launch SteamVR implicitly.
- [ ] Test concurrent clients, disconnect during a command, duplicate request IDs, timeout with later outcome lookup, service restart, socket permissions, and engine shutdown cleanup.

**Exit gate:** a test client can manage two scripts through the socket without racing frame mutation; the offline start path works on Deckard without starting SteamVR implicitly.

## Step 8 — CLI and scratch REPL

- [ ] Implement `pocketspace-cli` commands from the approved plan, including `start-engine` while the engine socket is offline. Make all one-shot commands noninteractive and deterministic with `--json`, stable exit codes, stderr diagnostics, explicit timeouts, and JSONL `watch`.
- [ ] Create persistent scratch scene/context at ID `0`. The default REPL evaluates there and can create objects/panels, manage scripts, and publish events. `:enter <id>` evaluates in a selected live script; `:leave` returns to scratch. Targeted evaluation checks instance generation. Script reload/stop returns attached REPLs to scratch; `scratch reset` affects only ID `0`.
- [ ] Serialize evaluation from multiple clients; capture result, exception, and `print()` output per request. Bound source/output/runtime and interrupt tight JavaScript loops without stopping other instances. Never auto-retry an evaluation with an unknown outcome.
- [ ] Test a real interactive terminal session and a non-TTY harness that parses JSON, watches a bounded stream, checks uncertain outcomes, enters/leaves a script, and edits both scratch and a user scene.

**Exit gate:** a harness can launch, monitor, publish, evaluate, and stop scripts without parsing human text; an interactive user can create an object in scratch and then enter a script safely.

## Step 9 — Resource telemetry

- [ ] Report engine-wide FPS, CPU, RSS, and DRM GPU memory once. For each script, expose QuickJS heap, object/panel/asset/body counts, measured callback time, and owned allocation estimates where attribution is valid. Expose scratch and event-queue/drop metrics.
- [ ] Do not divide process totals among scripts or add shared RAM and GPU totals. Mark unavailable values and sample intervals. Bound telemetry work and cadence so monitoring does not disturb the 90 Hz loop.
- [ ] Test metrics for two scripts with different workloads, stop/reload cleanup, unavailable DRM data, and a monitoring client disconnect. Compare telemetry overhead before enabling frequent polling.

**Exit gate:** CLI JSON and dashboard protocol show consistent totals and per-instance values, with estimates clearly labeled.

## Step 10 — Native SteamVR dashboard

- [ ] Build a separate dashboard process modeled on Framecorder's manifest and SteamVR-bound user service. It attaches only to an existing SteamVR session and reconnects when the engine starts or restarts.
- [ ] Add offline Start Engine, script catalog/launch/stop/reload, autostart/restart settings, error alerts, resource display, and a clear scratch entry. Send SteamVR notifications for callback-error stops even when the tab is closed; avoid duplicate notifications during a restart loop.
- [ ] Refresh at a modest cadence while visible and sleep when hidden. Capture pointer input only while the tab is active. Keep dashboard crash/restart independent of the engine and scripts.
- [ ] Test offline/online transitions, two scripts, stopping a held object, script errors, service restarts, and dashboard focus in-headset. Use Framey/Deckloader only as reference; the first release is a native dashboard tab.

**Exit gate:** the user confirms the tab's controls, notifications, resource display, and two simultaneous scenes in the headset. Stop the test demo and verify it exited.

## Step 11 — Full regression and documentation pass

- [ ] Update `docs/architecture.md`, `docs/lifecycle.md`, `docs/javascript-api.md`, `docs/scene-persistence.md`, `docs/getting-started.md`, `docs/agent-handoff.md`, the test index, and the smallest useful examples. Keep these pages descriptive of shipped behavior, and remove completed work from `docs/remaining-work.md`.
- [ ] Run focused native regressions plus `lifecycle-script-regression`, `lifecycle-input-regression`, `scene-persistence-regression`, `scene-container-regression`, `examples-regression`, physics and render checks affected by ownership changes. Record exact commands and results, not only test names.
- [ ] Run documentation lint and site build on Sigma. Build production ARM64 engine on Deckard with the documented CMake configuration; run a bounded two-script noninteractive soak, then obtain headset acceptance for dashboard, interaction arbitration, and rendering. Keep automated, measured, and user-observed evidence separate.
- [ ] Confirm graceful teardown leaves no demo process, leaked overlays, or stale control socket. Check clean Git status, review the diff, commit focused changes, and push when requested.

**Exit gate:** all approved behavior in the design plan is implemented and verified, with any remaining limitation recorded explicitly.

## Step 12 — Completion record

Fill in this table as work progresses. Do not mark a step complete from a passing build alone.

| Step | Commit | Sigma checks | Deckard checks | Remaining evidence |
| --- | --- | --- | --- | --- |
| 0 | Step 0 ownership-audit commit | Four headless regressions, fixture syntax, docs lint/site build pass | Matched one-scene baseline recorded in Step 2 | None |
| 1 | Step 1 headless-registry commit | Fixture syntax, docs lint/site build pass | ARM64 production/new lifecycle targets build; new, old lifecycle, examples regressions and non-VR model inspect pass | Production frame loop in Step 2; scratch context in Step 8; event routing in Step 3 |
| 2 | Instance-scoped scene traversal, catalog startup, and stop cleanup | Docs lint/site build and diff check pass | ARM64 engine and focused regressions pass; empty startup, two-scene runtime, injected error, and two reloads pass; offscreen overlapping-ID render and user headset visibility pass | External stop command in Step 7; scratch scene in Step 8 |
| 3 | Model-ray arbitration, panel owner routing, and per-script disconnect cleanup | Lifecycle input and grab-button regressions, docs lint/site build, diff check pass | ARM64 engine and focused lifecycle, arbitration, scene-container, and panel-submission regressions pass; user confirmed overlapping targets and independent two-hand grabs | None |
| 4 | Pending | Pending | Pending | Pending |
| 5 | Pending | Pending | Pending | Pending |
| 6 | Pending | Pending | Pending | Pending |
| 7 | Pending | Pending | Pending | Pending |
| 8 | Pending | Pending | Pending | Pending |
| 9 | Pending | Pending | Pending | Pending |
| 10 | Pending | Pending | Pending | Pending |
| 11 | Pending | Pending | Pending | Pending |
