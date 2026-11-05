# Control protocol

[`pocketspace-cli`](cli.md) is the command-line client for this protocol. The engine accepts management requests on a local Unix-domain socket. Tools such as
the CLI and dashboard use it to list, start, stop and reload scripts, change
settings, evaluate JavaScript, publish bus messages and watch for changes.

The socket is `$XDG_RUNTIME_DIR/pocketspace/control.sock`, or the path given with
`--control-socket`. Its directory is private to the user (mode `0700`) and the socket
is mode `0600`. Anyone who can connect can run code in the engine as that user, so
never relax these permissions or forward the socket. The socket also keeps a second
engine from starting: an engine that finds another engine answering exits with code
14. A socket left behind by a crashed engine is replaced.

## Messages

Each request and each response is one JSON object on one line, at most 1 MiB. A
longer line gets a `too_large` error and the connection is closed. Requests run on
the engine's host thread between frames, in the order they arrive, so they never
race with scene updates. Up to 64 requests run per frame.

```json
{"v": 1, "id": "req-17", "op": "start", "name": "tools/notes.js"}
```

`v` must be `1`. `id` is a string of 1 to 128 bytes chosen by the client; it comes back
in the response. A successful response carries `result`:

```json
{"v": 1, "id": "req-17", "ok": true, "result": {"instanceId": 3, "name": "tools/notes.js", "state": "running", "generation": 1}}
```

A failure carries `error`, with a stable `code`, a `message`, and sometimes more fields:

```json
{"v": 1, "id": "req-18", "ok": false, "error": {"code": "stale_generation", "message": "instance has a newer generation", "generation": 2}}
```

| Code | Meaning |
| --- | --- |
| `bad_request` | Not a JSON object, or a missing or mistyped field. `id` is `null` when the request ID itself was unusable. |
| `unsupported_version` | `v` is not `1`. |
| `unknown_op` | No such operation. |
| `not_found` | Unknown instance, catalog entry or request ID. |
| `stale_generation` | The instance has reloaded or restarted since that generation; `generation` gives the current one. |
| `conflict` | That script is already running. |
| `invalid` | The request is well formed but not allowed, such as stopping scratch or invalid settings. |
| `load_failed`, `reload_failed` | The script did not load; `instance` describes it. A failed reload keeps the previous scene. |
| `timeout` | An evaluation ran past its limit. |
| `too_large`, `busy`, `unavailable` | A request over the size limit, more than 16 clients, or a resource the engine cannot reach. |

### Instances

Status results describe one instance:

| Field | Meaning |
| --- | --- |
| `instanceId` | Stable for the engine session. `0` is the scratch scene. |
| `name`, `path` | Catalog name and canonical file path; scratch is named `scratch`. |
| `state` | `running`, `stopped` or `failed`. |
| `generation` | Increases with every successful reload or restart. |
| `uptimeSeconds` | Time since the current generation started while running, else 0. |
| `lastError` | The last load or callback error, or `null`. |
| `restartPending` | Whether an automatic restart is scheduled. |
| `lastReload` | `"ok"`, `"failedKeptScene"`, or `null` before any reload. |

Operations that target an instance take `instanceId` and its current `generation`, so
a request meant for an older scene fails instead of touching its replacement.

## Operations

| Operation | Fields | Result |
| --- | --- | --- |
| `catalog` | | `{scripts}`: every catalog name. |
| `list` | | `{instances}`: every instance this session, including scratch and stopped scripts. |
| `status` | `instanceId` | One instance. |
| `start` | `name` | The new instance. |
| `stop` | `instanceId`, `generation` | The stopped instance. A manual stop also cancels a pending restart. |
| `reload` | `instanceId`, `generation` | The instance after reloading from its file. |
| `settings.get` | | `{path, settings, fileError}`. |
| `settings.set` | `settings` | Validates and atomically writes the whole [settings file](lifecycle.md#startup-and-recovery), then applies it. |
| `stats` | | Engine totals and each instance's [resources](#resources). |
| `watch` | | `{watching, seq}`, then events on this connection. |
| `publish` | `topic`, `payload` | `{sequence}`, or `null` when the bus is full. Publishes as scratch, source `0`. |
| `eval` | `code`, optional `instanceId` (default `0`), `generation` (required unless `0`), `timeoutMs` (1 to 10000, default 1000) | `{result, exception, output, instanceId, generation}`. |
| `scratch.reset` | | The new scratch instance. |
| `outcome` | `requestId` | `{response}`: the remembered response of a recent changing request. |

`start`, `stop`, `reload`, `settings.set`, `publish`, `eval` and `scratch.reset`
change engine state. The engine remembers the responses of the last 256 of these by
request ID. Sending one again with the same `id` returns the first response with
`"duplicate": true` instead of running it again. After a lost connection or a client
timeout, ask `outcome` before retrying, and never resend an `eval` whose outcome is
unknown.

### Evaluation

`eval` runs code in a live scene's existing context and changes that scene
immediately; it is not a transactional reload. `result` is the value as JSON text
when it has a JSON form, otherwise its string form. `exception` is the thrown
error's text, or `null`. `output` holds that request's `print()` lines. Source is
limited to 256 KiB, and `result` and `output` to 64 KiB each.

Code that runs past `timeoutMs` is interrupted. Its scene is then in an unknown
state, so a user script stops as if a callback had failed, and scratch starts again
empty. Native calls that block cannot be interrupted, but engine bindings do bounded
work.

### Scratch

Instance `0` is the engine's scratch scene: an always-running context with the normal
scene API and no file. It lasts for the engine session. `scratch.reset`, or an
uncaught error in one of its callbacks, recreates it empty. It is never stopped or
restarted by the automatic restart policy. Its `scripts` object
[manages other scripts](cli.md#managing-scripts-from-scratch) through the same
operations.

### Events

After `watch`, the connection also receives event lines with a sequence number that
increases across the session:

```json
{"v": 1, "event": "status", "seq": 12, "instance": {"instanceId": 3, "state": "failed"}}
{"v": 1, "event": "error", "seq": 13, "instanceId": 3, "message": "Error: boom"}
{"v": 1, "event": "busDrop", "seq": 14, "dropped": 3, "total": 3}
{"v": 1, "event": "resources", "seq": 15, "fps": 90.0, "instances": [...]}
```

`status` follows every state or generation change, `error` follows a failure, and
`busDrop` reports messages dropped by a full bus. `resources` follows each
once-a-second sample while at least one client is watching. Its fields are those
of the `stats` result. A client that stops reading for more than 4 MiB of output
is disconnected; reconnect and `list` to resynchronize.

### Resources

`stats` and the `resources` event report the engine's process totals once, then
each instance's own values. Process totals are never divided among scripts, and
no value adds shared memory into a script. A value that cannot be measured is
`null`.

| Field | Meaning |
| --- | --- |
| `fps`, `cpuPercent`, `ramBytes`, `vramBytes` | Process measurements from the latest sample, as [`getEngineStats`](javascript-api.md#engine-statistics) defines them. `vramBytes` is `null` when the GPU driver does not report it. |
| `gpuEstimateBytes` | Estimated GPU allocations of the whole engine: every running script's own estimate plus `sharedGpuEstimateBytes`. |
| `sharedGpuEstimateBytes` | Estimated allocations scripts can share: lighting environments and the sheen table, each counted once. |
| `sampleSeconds`, `sampleId` | The latest sample's interval, and its number (0 before the first sample, when sampled values are `null`). |
| `bus` | `{queued, queuedBytes, dropped}` for the whole event bus. |
| `instances` | One entry per instance, below. |

| Instance field | Meaning |
| --- | --- |
| `instanceId`, `name`, `state`, `generation` | As in [status results](#instances). |
| `heapBytes` | Memory the script's QuickJS runtime has allocated, read when asked. |
| `objects`, `panels`, `assets`, `bodies` | Current counts in the script's scene; `bodies` counts physics bodies. |
| `gpuEstimateBytes` | Estimated GPU allocations the script owns, from the latest sample: its surfaces, model buffers and textures, deformed buffers, panels, instancing buffer and shadow map. An estimate from sizes, not a driver measurement. |
| `callbackMsPerSecond` | Time its update, input and bus callbacks took over the latest sample, in milliseconds per second of wall time. These add up across scripts; 1000 would be one core fully busy. |
| `worstCallbackMs` | The longest single frame of its callbacks in that sample. |
| `busQueued`, `busDropped` | Its undelivered publications, and its publications dropped this session. |

For a stopped or failed instance, every field from `heapBytes` to `worstCallbackMs`
is `null`; the bus counters remain. Callback values are `null` until the first
sample after a script starts or restarts.

## Running the engine as a service

`tools/install-user-service.sh` installs two systemd user units for the current
checkout: `pocketspace.service` runs the engine, and `pocketspace-autostart.service`
starts it when SteamVR starts, but only while the settings file has
`"engineAutostart": true`. Without that setting it does nothing.

```sh
tools/install-user-service.sh
systemctl --user start pocketspace.service   # start the engine now
systemctl --user stop pocketspace.service
tools/install-user-service.sh --remove
```

The service restarts the engine after a crash, except when it exits because of
invalid arguments or because another engine is already running.

## Example

```python
import json, os, socket
path = os.path.join(os.environ["XDG_RUNTIME_DIR"], "pocketspace/control.sock")
with socket.socket(socket.AF_UNIX) as s:
    s.connect(path)
    s.sendall(b'{"v":1,"id":"1","op":"list"}\n')
    print(json.loads(s.makefile().readline())["result"]["instances"])
```
