# Scene ownership containers

The production API adds flat lifetime containers for objects, panels and optional
ownership of the existing single input callback. Shared model assets and rendering
policies retain scene lifetime. [The contract](../../docs/scene-containers.md)
documents root ownership, transfers, disposal and resource sharing.

## Native binding and lifecycle validation

The real QuickJS bindings pass `scene-container-regression`:

- Strict integer IDs, unknown/disposed IDs, root transfer, active-container limit,
  throwing getters and reentrant option evaluation that disposes an owner.
- Individual removal and container disposal, idempotent repeated disposal, live
  membership snapshots and transfer between containers/root.
- Physics bodies and attached cross-container joints cleaned up while outside
  bodies survive; shared model assets remain cached.
- Interactive panel ownership survives initialization, and removed panels enter
  the existing deferred-retirement path.
- Owned callbacks safely dispose their own owner during an event batch. Failed
  script candidates preserve active callback ownership and scene state.
- Shared render surfaces survive removal of an owned member while a root member
  remains; last-member removal invokes normal release. Pointer/hover references
  are removed. Repeated disposal and successful reload clear the remaining scopes.

The existing script-lifecycle regression also passes, and all 24 final examples
load with the real bindings, including `scripts/containers.js`.

## Live production resource cycles

A 32-second bounded normal-engine run creates and disposes a tool three times
without replacing its script. A root object survives in the same render group,
with a persistent root panel. The tool owns eight physics objects, a panel and
an input callback; a joint connects an owned object to the outside root body.
All six phases pass ownership/count assertions and report clean native logs.

| Phase | Tool | Objects | Panels | Final sampled owned GPU estimate |
|---:|---|---:|---:|---:|
| 0 | Open | 9 | 2 | 52,468,972 bytes |
| 1 | Closed | 1 | 1 | 8,412,396 bytes |
| 2 | Open | 9 | 2 | 52,468,972 bytes |
| 3 | Closed | 1 | 1 | 8,412,396 bytes |
| 4 | Open | 9 | 2 | 52,468,972 bytes |
| 5 | Closed | 1 | 1 | 8,412,396 bytes |

Closed phases retain the root resources and cached shared model, as intended.
The consistent sampled estimates are evidence for this bounded scenario, not
an allocator/RSS leak audit or a claim that driver caches return to zero.
Shutdown releases all grouped-budget charges. No demo remains running.
Interactive headset disposal/reopening while grabbing remains a validation item.

## Artifacts and reproduction

- [Live phase measurements](scene-containers-live-frame.json).
- [Binding/lifecycle validation and tested source/binary hashes](scene-containers-validation-frame.txt).
- [Build and run commands](../README.md#scene-ownership-containers).

The base is `a9ae66d` plus this change. Raw Frame logs are under
`/tmp/sf-containers-live`, `/tmp/sf-containers-regression.log`,
`/tmp/sf-containers-lifecycle.log` and `/tmp/sf-containers-examples.log`.
