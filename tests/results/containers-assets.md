# Nested containers and model lifetime

Validated on Steam Frame on October 30, 2026.

## Automated correctness

The native `asset-lifetime-regression`, `scene-container-regression`,
`scene-persistence-regression`, `examples-regression`,
`animation-blend-regression`, `directional-light-regression`,
`point-light-regression` and `spotlight-regression` targets passed.

Coverage includes recursive ownership and cleanup, reparenting/cycle rejection,
v11 hierarchy capture and fresh-ID restoration, legacy flat snapshots,
malformed/capacity/reservation failure rollback, hidden and live asset users,
GPU buffer/texture and physics cache release, stable invalid asset IDs,
repeated unload/reload, restoration after unload, and live-asset capacity.
The lifetime check completed without GL errors or retained PBR LOD budget.

## Headset acceptance

The user verified `scripts/nested-containers.js`: grabbing, recursive close and
reopen, detaching a child that survives parent disposal, and snapshot restoration
all worked.

The user verified `scripts/asset-lifetime.js`: capture the car, close its
container, unload its unused model, then restore and interact with it again.
Unloading has no visible effect because the object must already be removed;
automated resource checks establish release, rather than headset appearance.
A status display would make this example's sequence clearer.

Both demos were stopped after verification; process checks confirmed exit.
These checks establish correctness and visual acceptance, not a measured
performance improvement or an immediate reduction in process RSS.
