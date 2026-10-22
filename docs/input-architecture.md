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

Native updates merge component deltas into per-hand state, invalidate snapshots
on disconnect, and populate independent polling/callback queues. Script getters
read cached state rather than issuing synchronous hardware queries. Callback
delivery runs on the script thread in the host loop. Controller roles and poses
are refreshed separately from the raw stream.

Custom controller beams pick transformed model triangles and drive native grabs
from raw allowed-button state. Native rectangular surface hover is a separate
signal and cannot substitute for a mesh hit when acquiring a custom-beam grab.
SteamVR delivers interactive panel pointers in panel pixel coordinates.

Gaze polling is opt-in per loaded script and uses a separate action set. Any
nonzero primary scene-process PID suppresses gaze action polling. Foveation
centers come from a separate read-only system call. See [tracking](tracking-api.md)
for exact validity and coexistence contracts, [JavaScript API](javascript-api.md)
for input/event fields, and [lifecycle](lifecycle.md) for reconnect behavior.
