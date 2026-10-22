# Steam Frame overlay engine: remaining work

Status: October 21, 2026. Source of record: Sigma's `/home/zetaphor/Code/sf-overlay-mvp`, commit `5c3c85b`; Deckard is the SteamVR build and headset test host.

## Verified baseline

- The avocado and box headset probe supports independent custom-laser grabs from both hands. The beams stop at the selected model, keep their grab length until push/pull or release, and quick hand motion does not release a model while its trigger is held.
- The two-object probe ran at about 90 FPS with `gl_error=0`. The panel test also verified readable rendering, panel pointer events, raw controller input, and clean script reloads.
- These results cover the tested scenes and headset session. They do not establish behavior for every model or larger scene.

## Next: selection and controls

1. **Validate model selection beyond the avocado and box.** The picker tests transformed triangles and uses a small bounds fallback for near misses. Exercise concave, thin, rotated, overlapping, and high-triangle models, including Toy Car and Lantern. Check false picks through gaps, nearest-object choice, per-hand isolation, hover stability, and the beam endpoint. Tighten or replace the fallback where it selects empty space. Done when both hands can select and release the intended model in those cases without selecting a nearer gap or an object behind it.
2. **Optimize high-triangle picking.** The avocado/box probe held 90 FPS, but the earlier Toy Car probe ran near 31 FPS with a 108,936-triangle asset. Measure render time separately from picking time, then add a suitable acceleration structure or cheaper broad phase if picking is material to the slowdown. Done when the chosen multi-model test scene meets an agreed headset frame-time target without lag or ghosting.
3. **Resolve laser pitch.** `laserPitchWithStickClick` is experimental: stick-click events were observed, but the user did not see pitch motion. Record smooth-scroll input while click and trigger are held, choose a nonconflicting control, implement it, and verify pitch direction and rate in the headset. Keep it out of normal demos until verified.
4. **Decide and implement a dedicated grip pickup gesture.** Near-hand pickup currently starts with the trigger. Define how grip pickup, laser grab, and release should interact, using the available raw grip input without relying on SteamVR native laser focus. Verify two-hand pickup and that panel/native pointer interactions still work.

## Input and reliability

5. **Protect the raw-input integration against SteamVR changes.** The `vrserver` WebSocket is undocumented. Compare the native monitor with `scripts/steam-frame-input.py` after SteamVR updates; verify discovery, reconnect, resubscription, button/value normalization, and behavior during transient tracking loss. Evaluate a documented action path only if it can provide the needed controls in this overlay context.
6. **Broaden lifecycle testing.** Exercise repeated scene reloads, object and panel removal, controller disconnect/reconnect, SteamVR restart, and shutdown while one or both models are held. Verify no stale grab state, ghost overlays, resource leaks, or retained input callbacks.

## Later engine milestones

7. **Scale scene performance.** Measure frame time, GPU/shared memory, overlay count, and Frametop responsiveness as objects, textures, panels, and per-eye resolution increase. The current 768-pixel default is the known usable starting point; 512 pixels looked too blurry, while three 2048-pixel surfaces lagged in headset testing.
8. **Add animation and rendering features.** Implement glTF animation playback, skinning, morph animation, and fuller materials/lighting. The current gallery loads sample assets but shows animated models in their default pose; sheen is only approximated.
9. **Define game-state features.** Specify and then implement scene lifetime rules, physics, and persistence for scripted experiences. Preserve full-sphere placement and coexistence with the primary VR app.

## Verification rule

For each headset-facing change, record the exact scene, Deckard build revision, runtime log, and what the wearer actually saw or felt. A successful build, OpenVR call, or event log alone does not establish headset behavior.
