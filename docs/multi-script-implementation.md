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
- [ ] Record a representative one-script Deckard FPS/memory baseline with an active headset and noninteractive overlay when the headset is available. If unavailable, mark this measurement pending and collect it before evaluating Step 2's two-script runtime cost.

**Exit gate:** the ownership map covers creation, candidate reload, successful reload, failed reload, manual stop, callback error, and process shutdown. Baseline headless checks pass without changing behavior; the one-script device measurement is recorded or explicitly tracked for completion before Step 2 performance comparison. No interactive headset test is needed yet.

## Step 1 — Instance registry and script lifecycle

- [ ] Extract script loading/teardown from the single `ScriptHost` path in `src/main.cpp` into a registry with stable positive instance IDs and reserved scratch ID `0`. Preserve the existing separate QuickJS runtime/context and scene per script.
- [ ] Keep `currentScene` (or its replacement) scoped to a single callback/evaluation; restore the prior scope after it returns or throws. Candidate evaluation must never mutate another active scene. Refactor the one global input callback into instance ownership before two callbacks can coexist.
- [ ] Add launch, reload, manual stop, and callback-error stop transitions. One live instance per canonical file; a duplicate launch returns a structured error. Manual stop suppresses restart for this engine session. Successful reload replaces only that instance; failed reload preserves its old scene. Subscription handling arrives in Step 5.
- [ ] Keep the legacy positional script invocation. Managed mode starts with scratch only. Keep automatic file-change reload for each running file.
- [ ] Add native lifecycle regressions for two independent scenes, colliding local IDs, candidate rejection, failed reload, one-instance stop, callback exception, and shutdown cleanup. Adapt `tests/lifecycle-script-regression.cpp`; add a focused multi-instance target if needed.

**Exit gate:** two scripts can be loaded and stopped independently in a headless harness; one-script regressions still pass. No renderer or live headset claim yet.

## Step 2 — Shared frame, rendering, and resource cleanup

- [ ] Change renderer traversal to iterate a stable snapshot of live instances. Qualify native surface, panel, and overlay keys by instance ID while leaving JavaScript IDs local to their scene. Preserve stereo submission, draw order, transparency, GPU ownership, and per-scene physics.
- [ ] Make successful reload, failed reload, stop, and error stop release or retain only the appropriate instance's surfaces, panels, models, lights, shadows, and render caches. Keep process-wide caches shared only where their keys and resource lifetime are valid across instances.
- [ ] Keep one OpenVR session, EGL context, input poller, and compositor loop. Resolve the shared anchor once at startup. Ensure scratch can render objects while no user script runs.
- [ ] Add offscreen/native tests with two visible scenes and overlapping numeric IDs. Include failure injection and repeated start/stop/reload cycles. Use relevant existing render, panel, asset-lifetime, and scene-container regressions to detect lost ownership.

**Exit gate:** two scenes render in offscreen tests with no overlay-key collision or leaked resources. On Deckard, build the engine and run a bounded noninteractive two-scene smoke only when the headset/runtime is available; mark visual acceptance pending until observed in-headset.

## Step 3 — Input arbitration and event routing

- [ ] Sample tracking and raw controller input once per frame. Fan out read-only snapshots without allowing one script's polling API to drain another's queue. Deliver callbacks only to their owning instance.
- [ ] Arbitrate model and panel targets across all instances. Default to closest eligible hit; optional per-script priority wins first, then distance and stable instance ID. Keep a held object attached to its owner until release or that instance's stop/reload.
- [ ] Qualify hover, grab, panel pointer, and laser state by instance. Clear only affected state when one script stops or reloads; preserve valid state in the other scripts.
- [ ] Test overlapping objects/panels, equal-distance ties, priority override, both hands, disconnect/reconnect, stop while held, and callback-triggered mutation. Run `lifecycle-input-regression` and the relevant grabbing/panel checks.

**Exit gate:** exactly one owner receives each arbitrated interaction; stopping one script does not clear another's input state. Headset acceptance of overlap and grabbing remains a separate check.

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

- [ ] Add a configurable user scripts root separate from repository examples. Recursively list readable `.js` files under that root; canonicalize and reject traversal and symlink escapes. Use stable catalog keys for launch, autostart, saved scenes, and per-script priority.
- [ ] Put the autostart list, optional priorities, global automatic-restart flag, and engine-autostart setting in a validated configuration file. Dashboard and CLI changes write this one file atomically. Reload valid external edits and retain the last good settings on an invalid edit.
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
- [ ] Add offline Start Engine, script catalog/launch/stop/reload, autostart/priority/restart settings, error alerts, resource display, and a clear scratch entry. Send SteamVR notifications for callback-error stops even when the tab is closed; avoid duplicate notifications during a restart loop.
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
| 0 | Step 0 ownership-audit commit | Four headless regressions, fixture syntax, docs lint/site build pass | No runtime workload | One-script FPS/memory baseline before Step 2 comparison |
| 1 | Pending | Pending | Pending | Pending |
| 2 | Pending | Pending | Pending | Pending |
| 3 | Pending | Pending | Pending | Pending |
| 4 | Pending | Pending | Pending | Pending |
| 5 | Pending | Pending | Pending | Pending |
| 6 | Pending | Pending | Pending | Pending |
| 7 | Pending | Pending | Pending | Pending |
| 8 | Pending | Pending | Pending | Pending |
| 9 | Pending | Pending | Pending | Pending |
| 10 | Pending | Pending | Pending | Pending |
| 11 | Pending | Pending | Pending | Pending |
