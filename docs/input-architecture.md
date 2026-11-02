# Input architecture

The native host combines three sources into cached JavaScript snapshots:

| Source | Responsibilities |
| --- | --- |
| OpenVR tracked poses | Controller/HMD transforms and controller role assignment. |
| OpenVR overlay events and action bindings | Native surface/panel pointers, optional action snapshots, haptics, gaze actions, and system foveation centers. |
| Local vrserver WebSocket | Global controller boolean/scalar component deltas, including button, axis, and capacitive values. |

The raw input monitor subscribes directly to left/right controller mailboxes.
It does not depend on overlay hover or activation of an action set. This
vrserver interface is undocumented, so compatibility depends on SteamVR's
implementation. Unsupported structured component values are invalidated;
finite numbers and booleans retain real zero/false values.

The engine polls the physical raw stream once per frame, merges component deltas
into shared per-hand state, then copies raw events into each running script's
independent polling/callback queues. A script cannot drain another script's
events. Disconnect invalidates the affected hand and clears its pending raw
events from every script queue; reconnect resumes delivery. Script getters read
cached state rather than issuing synchronous hardware queries. Callback delivery
runs on the host thread with the owning script's scene selected. Controller roles
and poses are refreshed separately from the raw stream.

Custom controller beams pick transformed model triangles across running scenes.
The closest eligible model hit wins per hand; equal distances use stable instance
ID order. A held object keeps its owner until release or that script stops or
reloads. Native rectangular surface hover is a separate signal and cannot
substitute for a mesh hit when acquiring a custom-beam grab. SteamVR selects
interactive panel overlays and delivers pointers to their owning scripts in
panel pixel coordinates.

Gaze polling is opt-in per loaded script and uses a separate action set. Any
nonzero primary scene-process PID suppresses gaze action polling. Foveation
centers come from a separate read-only system call. See [tracking](tracking-api.md)
for exact validity and coexistence contracts, [JavaScript API](javascript-api.md)
for input/event fields, and [lifecycle](lifecycle.md) for reconnect behavior.

Opt-in [physics contact events](physics-api.md#collision-events) join the same
callback/polling queues after the simulation step. Solver callbacks only cache
contact data; they never call JavaScript. Collision delivery does not depend on
the vrserver connection, and controller reconnect preserves queued collisions.

[Joint-break events](physics-api.md#breakable-joints) use the same callback and
poll queues, independently of collision-event opt-in. Their object IDs retain
the joint creation order; the joint is already removed when delivery occurs.
