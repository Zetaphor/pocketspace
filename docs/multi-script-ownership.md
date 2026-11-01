# Multi-script ownership inventory

Status: Step 0 implementation evidence, based on `b417ef7` before runtime changes. Read with the [design contract](multi-script-plan.md) and [execution guide](multi-script-implementation.md). This is a source audit, not a claim that two live scripts work yet.

At the Step 0 audit, the CLI accepted one positional script path and defaulted to `scripts/avocado.js`. `main` loads that script before EGL/OpenVR initialization, then checks its modification time every 30 frames and attempts a transactional replacement. Ctrl-C, SIGTERM, or a SteamVR quit event ends the single engine process. The later Step 2 implementation replaced this startup path with an empty user-script list and catalog launches.

## Current ownership

| State | Current source | Required owner | Why it matters |
| --- | --- | --- | --- |
| QuickJS runtime/context, `update`, file timestamp, `Scene` | `ScriptHost` and `loadScript`/`closeScript` in `src/main.cpp` | Instance | Already grouped for one active script; candidate reload has a separate runtime and scene. A registry must retain several hosts and identify each by stable instance ID. |
| Objects, panels, retired panels, assets, lights, physics world, containers, next local IDs | `Scene` in `src/main.cpp` | Instance | IDs restart at 1 in every scene. Native lookups must use `(instance ID, local ID)`; JavaScript IDs stay local. Disposal must not touch another scene's resources. |
| `currentScene`, `liveScene` | File statics in `src/main.cpp` | Scoped current evaluation; registry for live scenes | Most JS bindings dereference `currentScene`. Candidate evaluation, callbacks, REPL evaluation, and event dispatch must enter and leave the correct scope, including exception paths. A single `liveScene` pointer cannot represent several running scenes. |
| Input callback context/value/container | File statics in `src/main.cpp` | Instance | `onInputEvent` currently replaces a global callback. A candidate preserves/restores the previous callback. Two scripts need independent callback references and cleanup. |
| Controller snapshots, tracking anchor, OpenVR action input, raw vrserver subscription | File statics and host-loop locals in `src/main.cpp`; `src/controller_input.cpp`; `src/frame_input.cpp` | Engine | Read once per frame. Scripts receive immutable snapshots; one script's polling must not consume another's data. Keep one OpenVR/vrserver connection. |
| Raw-event and callback-event queues, hovered object IDs | `frame_input::Monitor` in `src/frame_input.*` | Per-instance delivery queues; engine raw source | `takeEvents()` and `takeCallbackEvents()` drain one global queue, and `resetScene()` clears it. Events need owner routing; a stop/reload must clear only that owner's deliveries. |
| Hovered object IDs, pointer snapshots, panel hover/cursor, mesh-hit history, `grabNeedsRelease`, `grabs` | File statics and host-loop locals in `src/main.cpp` | Per-hand arbiter plus instance-qualified target state | Current values hold bare local IDs. Cross-script picking chooses one owner per hand. Disconnect and stop must release only affected holds and not mistake another script's ID 1 for the same target. |
| Surface map, panel overlay/submission maps, grouped tile/adaptation state | Host-loop locals in `src/main.cpp` | Instance or keys containing instance ID | Maps use integer object/panel/group IDs. `surfaceMembers` traverses one `script.scene`; reload clears every surface and panel. Keep compositor allocation and admission budgets engine-wide where they count shared resources. |
| OpenVR overlay names | `createSurface`, object reservation, and panel reservation in `src/main.cpp` | Globally unique `(instance, local key)` | Keys now use `sf_overlay_engine.<id>` and `sf_overlay_engine.panel.<id>`. Two scenes with object/panel ID 1 would collide. Staging keys also need instance qualification. |
| EGL/GBM/DRM handles, shader programs, renderer statistics, compositor timing | Host-loop locals and file statics in `src/main.cpp`; `src/runtime_stats.h` | Engine | Keep one device/session. Shared resource accounting remains a process total; script metrics count only resources attributable to that instance. |
| Asset and prepared-PBR caches, instancing, lighting selection, shadow map | `Scene`, host-loop renderer caches, `src/pbr_variants.h`, `src/spot_shadows.h` | Explicitly classify each cache before multi-scene traversal | Caches keyed by local asset/object/light IDs can alias between scenes. Check capacity and resource lifetime across start/stop/reload before deciding which are shared and which are instance-owned. |
| Scene-save directory | `scenePersistence::directory` in `src/scene_persistence.h`, set by `--scene-state-dir` | Engine root plus stable per-script namespace | Current saves use one named-file directory. Resolve the caller's stable catalog key or `scratch` when saving/loading; an explicit import crosses namespaces through staged restore. |
| Process stats and `getEngineStats()` | `engineStats` and `jsGetEngineStats` in `src/main.cpp` | Process sampler plus instance-specific counters | CPU/RSS/DRM values describe the engine process. Current object/panel/physics counts use `currentScene`. Keep those per instance and label allocation estimates. |

## Lifecycle paths that must remain isolated

1. **Initial launch:** canonicalize the catalog path, reserve an ID, evaluate in a detached candidate, and publish only that instance on success. A failed launch leaves other instances untouched.
2. **Automatic or explicit reload:** read the changed script and stage a replacement scene. On failure, retain the old runtime, callbacks, event queue, overlays, grabs, and scene. On success, detach only that instance's native resources, release its old runtime, and publish its new generation.
3. **Manual stop:** disable that instance's autostart/retry for the current engine session, cancel its grabs/hover, detach its overlays, release its scene and queued events, and leave every other instance running.
4. **Callback error:** record the exception; stop only the owning user instance. For scratch ID 0, reset only scratch. Other callbacks and scenes continue.
5. **Process shutdown:** stop all instances before EGL/OpenVR teardown. A crash or SIGKILL cannot perform scoped cleanup; service restart must reestablish a fresh registry.

## First collision fixtures

`tests/scenes/multi-script-left.js` and `tests/scenes/multi-script-right.js` intentionally create object ID 1 and panel ID 1 in separate scripts, with the same JavaScript top-level variable names. Both panels are noninteractive. They are fixtures for the Step 1 lifecycle harness and Step 2 renderer/input checks; each can be loaded alone today, but concurrent loading is not implemented yet.

## Verification boundary

The Step 0 Sigma baseline uses `SF_BUILD_ENGINE=OFF` for `runtime-stats-regression`, `scene-lookup-regression`, `lifecycle-input-regression`, and `physics-regression`, plus documentation lint and site build. This does not link the OpenVR engine or show headset behavior. The one-script on-device FPS/memory baseline remains pending until an active headset is available; collect it before comparing two-script runtime cost in Step 2.
