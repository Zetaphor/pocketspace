# Command line and REPL

`pocketspace-cli` manages a running engine from a terminal or a program. It starts
the engine service, lists, starts, stops and reloads scripts, watches for changes,
publishes events and edits the autostart list. It also runs JavaScript in the
live scenes, either one command at a time or in an interactive REPL.

The build produces `build/pocketspace-cli`. It talks to the engine over the
[control socket](control-protocol.md), so it works only for the user who runs the
engine.

```sh
build/pocketspace-cli start-engine
build/pocketspace-cli start tools/notes.js
build/pocketspace-cli list
build/pocketspace-cli repl
```

## Commands

| Command | Does |
| --- | --- |
| `start-engine` | Starts `pocketspace.service` with `systemctl --user` if no engine answers, then waits for the socket. It never starts SteamVR. Needs [the user service](control-protocol.md#running-the-engine-as-a-service). |
| `list` | Every instance this session, including scratch (`0`) and stopped scripts. |
| `catalog` | Catalog names that `start` accepts. |
| `status <id>` | One instance. |
| `start <name>` | Starts a catalog script. The reply arrives after the script has loaded or failed. |
| `stop <id> [--generation N]` | Stops a script. |
| `reload <id> [--generation N]` | Reloads a script from its file. A failed reload keeps the running scene. |
| `stats` | Engine totals, then each script's [resources](control-protocol.md#resources): JavaScript heap, scene counts, estimated GPU allocations and callback time. |
| `watch [--limit N] [--duration S]` | Streams status, error, bus-drop and once-a-second resources events until the limit, the duration or Ctrl-C. |
| `publish <topic> [json]` | Publishes a `space.*` event from scratch, source `0`. |
| `scratch reset` | Clears and recreates only the scratch scene. |
| `data reset <name>` | Deletes a catalog script's [saves](scene-persistence.md#storage-namespaces) and saved data. A running script reloads from its defaults. Prints how many files were removed. |
| `autostart list` | Scripts started with the engine. |
| `autostart add <name>` / `autostart remove <name>` | Edits that list through the engine's validated settings write. `add` accepts only catalog names. |
| `eval --code TEXT` or `eval --file PATH` | Runs JavaScript in scratch. Add `--script ID [--generation N]` to run it in a live script, and `--eval-timeout MS` (1 to 10000, default 1000) to change its time limit. |
| `outcome <request-id>` | The engine's remembered reply to a recent changing request. |
| `repl [--eval-timeout MS]` | The [interactive REPL](#repl). |

`stop`, `reload` and `eval --script` target one generation of a script. Without
`--generation`, the CLI looks up the current generation first. A harness that
already knows the generation should pass it: if the script has reloaded or
restarted since, the command fails with `stale_generation` instead of affecting
the new scene.

Options go before or after the command:

| Option | Meaning |
| --- | --- |
| `--json` | Print one JSON object on stdout (`watch`: one per line). Messages for people still go to stderr. |
| `--socket PATH` | The control socket. Default `$XDG_RUNTIME_DIR/pocketspace/control.sock`. |
| `--timeout SECONDS` | How long to wait for a reply. Default 5, or 30 for `start-engine`. `eval` waits this long plus its evaluation limit. |
| `--request-id ID` | The request ID of a changing command (`start`, `stop`, `reload`, `publish`, `scratch reset`, `data reset`, `autostart add/remove`, `eval`), 1 to 128 bytes. Default: a new unique ID. |

## JSON output

With `--json`, every command prints exactly one line on stdout. It never prompts,
and it never writes terminal control codes. For engine requests, the line is the
engine's reply exactly as the [control protocol](control-protocol.md#messages)
defines it:

```json
{"v":1,"id":"cli-4121-1793900000000-1","ok":true,"result":{"instanceId":3,"name":"tools/notes.js","path":"/home/me/.local/share/pocketspace/scripts/tools/notes.js","state":"running","generation":1,"uptimeSeconds":0.000000,"lastError":null,"restartPending":false,"lastReload":null}}
```

Commands that combine several requests print the reply to the last one. `stop`
without `--generation` looks up the generation and then stops the script.
`autostart add` reads the settings, checks the catalog and then writes the new
list. Two commands build their own result in the same shape:

| Command | `result` |
| --- | --- |
| `start-engine` | `{"started": true}` when it started the service, `false` when an engine was already running, and `"socket"`. |
| `autostart list` | `{"autostart": [names]}` |

When the CLI cannot get a reply, it prints an error of the same shape with one of
these codes:

| Code | Meaning |
| --- | --- |
| `offline` | No engine answers on the socket. |
| `no_response` | A request that changes nothing got no reply in time, or the connection closed. It is safe to retry. |
| `unknown_outcome` | A changing request got no reply. It may or may not have run. `error.requestId` names it; see [uncertain outcomes](#uncertain-outcomes). |
| `usage` | Invalid arguments. `id` is `null`. |
| `start_failed` | `start-engine` could not start the service, or the engine did not answer in time. |

`watch --json` prints the engine's [event lines](control-protocol.md#events)
unchanged, plus two markers of its own. When the connection drops, it prints
`{"v":1,"event":"connectionLost"}` and keeps reconnecting. Once it is back, it
prints `{"v":1,"event":"reconnected","seq":N}`, where `N` is the engine's current
event sequence. Events in between are lost, so call `list` to resynchronize. An
engine restart starts its sequence again from 0.

## Exit codes

| Exit | Meaning |
| --- | --- |
| 0 | Success. |
| 1 | An unexpected failure. |
| 2 | Usage error. |
| 3 | The engine is not running (`offline`). |
| 4 | No reply in time, or the connection was lost (`no_response`, `unknown_outcome`). |
| 5 | `not_found`: unknown instance, catalog name, autostart entry or request ID. |
| 6 | `conflict`: the script is already running, or the autostart entry already exists. |
| 7 | `stale_generation`. |
| 8 | Rejected request: `invalid`, `bad_request`, `unknown_op`, `unsupported_version` or `too_large`. |
| 9 | `load_failed` or `reload_failed`. |
| 10 | `eval` ran, and the code threw an exception. |
| 11 | `eval` ran past its limit. The script was stopped, or scratch was recreated empty. |
| 12 | The engine is `busy` or a resource is `unavailable`. |
| 13 | `start-engine` failed (`start_failed`). |

## Uncertain outcomes

A changing request that times out or loses its connection may still have run. The
CLI never sends one again on its own. Instead it exits with 4 and reports
`unknown_outcome` with the request ID. To find out what happened:

1. Run `pocketspace-cli --json outcome <request-id>`. If it succeeds, `result.response`
   is the engine's original reply.
2. If it reports `not_found`, the engine never received the request, or it has
   since been pushed out of the last 256 remembered outcomes. Check `list` before
   you decide to run it again.

Sending a changing command again with the same `--request-id` is also safe. The
engine returns the first reply, marked `"duplicate": true`, without running it
again. Never resend an `eval` whose outcome is unknown under a new ID: its code
might already have changed the scene.

## REPL

`pocketspace-cli repl` runs JavaScript in the scratch scene, instance `0`. Scratch
has the normal scene API, so the REPL can load models, create objects and panels,
add physics, subscribe to topics and publish. Each input is evaluated
immediately in the live scene; it is not a transactional reload. Scratch's
objects and globals stay after the REPL exits, and every client shares them,
until `scratch reset`, an uncaught error in a scratch callback, or engine
shutdown.

```text
scratch> const box = createObject(loadModel('assets/samples/Box/Box.glb'), 0, 1.2, -.6, .2)
1
scratch> scripts.list().map(s => `${s.instanceId} ${s.name} ${s.state}`)
["0 scratch running","3 tools/notes.js running"]
scratch> :enter 3
entered 3 tools/notes.js, generation 1. Input now changes that live scene.
3:tools/notes.js@1> notes.length
2
3:tools/notes.js@1> :leave
in scratch
```

| Input | Does |
| --- | --- |
| `:enter <id>` | Runs following input in that running script's context. The prompt shows the instance, name and generation. |
| `:leave` | Returns to scratch. |
| `:where` | Shows the current target and generation. |
| `:help` | Lists these commands. |
| Ctrl-D | Exits. |

`print()` output appears above the result, and an exception appears as
`Uncaught ...`. If brackets, a template string or a block comment are still open,
input continues on a `... ` line; an empty line submits it as it is. The arrow
keys recall earlier input from the session. Ctrl-R searches it backwards: type
part of an earlier input, then press Enter to run the match.

When the entered script reloads, stops or fails, the REPL returns to scratch and
reports it before the next prompt. Input is always sent with the entered
generation, so it can never reach a replacement scene. Run `:enter` again to
continue in the new one. Input that runs past `--eval-timeout` is interrupted.
A script interrupted this way stops as if a callback had failed, and scratch is
recreated empty.

Without a terminal on stdin, the REPL prints no prompts or banner. It reads one
input per line and prints each result, so it can be driven through a pipe. For
programs, `eval --json` gives structured results instead.

### Managing scripts from scratch

Only the scratch scene has a `scripts` object:

| Call | Returns |
| --- | --- |
| `scripts.catalog()` | Catalog names. |
| `scripts.list()` | Every instance, as `list` describes them. |
| `scripts.status(instanceId)` | One instance. |
| `scripts.start(name)` | `undefined`; starts the script between frames. |
| `scripts.stop(instanceId, generation)` | `undefined`; stops it between frames. |
| `scripts.reload(instanceId, generation)` | `undefined`; reloads it between frames. |

`start`, `stop` and `reload` check their request immediately. They throw an
`Error` whose `code` is the protocol's error code, such as `not_found`,
`conflict`, `stale_generation` or `invalid`. Scratch code can run inside a frame,
so the change itself waits for the next frame boundary. Its outcome arrives on the
[engine topics](javascript-api.md#messages-between-scripts): `engine.scriptStarted`,
`engine.scriptStopped` (also when a start fails), `engine.scriptReloaded` and
`engine.scriptReloadFailed`. A request that is no longer valid by then, such as a
stop for a generation that has since reloaded, does nothing; the engine logs it.

```js
subscribe('engine.scriptStarted', event => print(`started ${event.payload.path}`));
scripts.start('tools/notes.js');
```

## Examples

A shell script that restarts a script and fails clearly:

```sh
#!/bin/sh
set -e
cli=build/pocketspace-cli
id=$($cli --json start tools/notes.js | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["instanceId"])')
$cli eval --script "$id" --code 'notes.length'
$cli stop "$id"
```

Python, checking exit codes and JSON:

```python
import json, subprocess

def cli(*args):
    done = subprocess.run(['build/pocketspace-cli', '--json', *args], capture_output=True, text=True)
    return done.returncode, json.loads(done.stdout)

code, reply = cli('start', 'tools/notes.js')
if code == 6:
    print('already running')
elif code:
    raise RuntimeError(reply['error'])
instance = reply['result']
code, reply = cli('eval', '--script', str(instance['instanceId']),
                  '--generation', str(instance['generation']), '--code', 'notes.length')
print(reply['result']['result'] if code == 0 else reply['error']['code'])
```

An agent harness should follow these rules:

- Run every command with `--json` and no terminal. Parse stdout as JSON, branch
  on the exit code and `error.code`, and treat stderr as log text only.
- Choose its own `--request-id` for each changing command and record it before
  sending.
- On exit 4 with `unknown_outcome`, query `outcome` with that ID before doing
  anything else. Repeat a command only under the same request ID.
- Keep the `generation` from `start`, `status` or `list`, and pass it to `stop`,
  `reload` and `eval --script`. On exit 7, read the instance again and decide
  whether to act on the new generation.
- Bound every stream: `watch --json --limit N` or `--duration S`. After a
  `connectionLost` or `reconnected` marker, run `list` again.
