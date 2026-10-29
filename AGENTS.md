# Working on this repository

## Start here

- Read [README.md](README.md) for setup, build commands, and examples.
- Read [docs/architecture.md](docs/architecture.md) for the engine pipeline and
  production optimization policies. Update it when those change.
- Read [docs/agent-handoff.md](docs/agent-handoff.md) before changing engine
  internals. It records ownership, rendering, input, and persistence invariants.
- Use [docs/remaining-work.md](docs/remaining-work.md) for unfinished work.
  It is a roadmap, not a prescribed implementation order.
- Use [tests/index.md](tests/index.md) to find relevant checks and headset scenes;
  [tests/README.md](tests/README.md) describes how to run them.

## Implementation

- This is a C++20 Linux/OpenVR engine with a QuickJS JavaScript API, GLES
  rendering, and Jolt physics. Keep host paths configurable through CMake.
- Follow nearby code conventions. Keep changes focused on the requested work.
- Preserve transactional script reload and scene restore: validate and stage
  changes before publishing them, and leave the active scene intact on failure.
- Resolve object/panel IDs again after mutations; do not retain pointers into
  scene vectors across insertion, removal, reload, or callbacks.
- Preserve independent per-object animation state, accurate picking/bounds,
  shared asset ownership, and the existing physics/interaction cleanup paths.
- Rendering optimizations must preserve stereo projection, draw order,
  transparency, compositor synchronization, and buffer lifetime. Consult the
  handoff and recorded comparisons before changing these contracts.
- Keep production defaults separate from test-only experiments. Profile before
  adopting optimizations, and compare equivalent workloads and quality settings.
- Preserve upstream asset credits and dependency licenses. Keep calibration
  files, runtime scene saves, generated builds, and profiling output out of Git
  unless intentionally adding a bounded test fixture or recorded test result.

## Build and verification

The default build targets Steam Frame's ARM64 environment:

```sh
cmake -S . -B build
cmake --build build -j2
```

For another host, override `SDK_INCLUDE`, `QJS_INCLUDE`, `QJS_LIB`, and
`STEAMVR_BIN` as described in the README. Reuse an appropriate configured build
directory; do not assume the local workstation has the headset dependencies.
Run `cmake -S . -B <build-directory>` again before building newly added targets.

- Regression targets are generally excluded from the default build. Build the
  relevant targets explicitly and follow their documented fixture/environment
  requirements. Preserve enabled assertions in regression targets.
- Run checks appropriate to the change. Exercise meaningful failure cases and
  resource cleanup when changing lifecycle, persistence, input, or ownership.
- Distinguish automated correctness, measured performance, and user-confirmed
  headset visual acceptance. Do not report one as evidence of the others.
- Run `git diff --check` before committing.

## Headset workflow

- Respect the user's current headset availability. Do not run headset workloads
  while they have asked to pause or said the headset is powered off.
- Check for an existing engine/demo process before launching another. Stop the
  relevant demo when requested, and verify that it exited. Avoid broad process
  kills or restarting SteamVR unless the task calls for it.
- After the user finishes headset verification, stop the relevant demo and verify
  that it exited before completing the task. Do not leave it running unless the
  user explicitly asks to keep it running.
- A request to stop a demo does not authorize relaunching it after subsequent
  code changes; wait for the user to continue headset testing.
- Prefer automated checks before asking the user to validate behavior in VR.
  Give brief controller instructions and concrete things to look for.
- Keep headset performance measurements comparable: use an active headset and
  record workload, quality settings, and CPU/GPU measurements where relevant.

## Documentation and examples

- Keep public behavior in the API guides under `docs/`. Update the relevant
  guide and smallest useful example when changing an API or its behavior.
- `scripts/` contains concise examples of supported functionality. Put test
  harnesses and experimental scenes under `tests/`, and measurement reports
  under `tests/results/`. Add new tests to `tests/index.md`.
- Document actual shipped behavior and limitations. Keep implementation history
  out of user guides, and remove completed items from the remaining-work list.
- Markdown remains the source of truth for both GitHub and the VitePress site.
  README inclusion regions share the build instructions and example inventory;
  preserve them and the example table's Markdown formatting.
- Run `python3 tools/lint-docs.py` after documentation/API/example changes.
  Run `npm run docs:build` after changing site content, links, or navigation
  (`npm ci` first when dependencies are not installed).
- See [docs/documentation-site.md](docs/documentation-site.md) for publishing.

## Completing work

- Keep commits focused, and report what changed, what was verified, and any
  remaining headset validation or known limitation.
- Preserve unrelated work. Commit only the intended files, and push when the
  user requests it. A local commit or headset deployment is not a remote push.
- Keep this file focused on contributor workflow. Keep detailed subsystem
  contracts in the handoff and API guides rather than duplicating them here.
