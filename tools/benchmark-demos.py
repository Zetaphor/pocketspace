#!/usr/bin/env python3
"""Measure the engine's resource usage with the demos as the workload.

Captures, in order (select with --captures):
  baseline  SteamVR only; runs only when this script starts the engine itself.
  idle      the engine with no scripts.
  each      every demo alone, one after another.
  all       every demo together.
  after     the idle engine again after all demos stopped (retained memory).

Each capture waits --settle seconds, then averages --samples one-second samples
of system and per-process CPU, RSS/PSS, DRM fdinfo GPU busy time and memory
(deduplicated by drm-client-id), and the GPU clock, while polling
`pocketspace-cli --json stats` every two seconds. The engine's [profile] lines
come from the pocketspace.service journal.

The script refuses to run while scripts other than scratch are running. It stops
the demos it started, and the engine if it started it. Results go to
build/benchmarks/<timestamp>/ (report.md and samples.json) unless --out is given.
Keep the headset active. What is in view changes GPU load, so one capture
cannot separate a code change from a different head pose: compare with --repeat
(three runs, alternating builds) and treat a change as real only when it moves
the median beyond the range. The script waits for Enter before the first
capture; --yes skips that once the wearer is ready.
"""

import argparse
import datetime
import json
import os
import pathlib
import statistics
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEMOS = ['clock', 'monitor', 'sketch', 'notes', 'basketball', 'desk-toys', 'pet-fox', 'lighting-studio']
CAPTURES = ['baseline', 'idle', 'each', 'all', 'after']
ENGINE_COMM = 'pocketspace'
COMPOSITOR_COMM = 'vrcompositor'
DEVFREQ = pathlib.Path('/sys/class/devfreq')
MIB = 1024 * 1024
CLK_TCK = os.sysconf('SC_CLK_TCK')


class CliError(RuntimeError):
    pass


class Cli:
    def __init__(self, path):
        self.path = str(path)

    def __call__(self, *args, timeout=60, check=True):
        done = subprocess.run([self.path, '--json', *args], capture_output=True, text=True, timeout=timeout)
        try:
            reply = json.loads(done.stdout.strip().splitlines()[-1])
        except (IndexError, json.JSONDecodeError):
            raise CliError(f'pocketspace-cli {" ".join(args)}: no JSON reply (exit {done.returncode}): {done.stderr.strip()}')
        if check and done.returncode:
            raise CliError(f'pocketspace-cli {" ".join(args)}: {reply.get("error")}')
        return done.returncode, reply


# ---- sampling ----------------------------------------------------------------

def read_text(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def pids_by_comm(comm):
    found = []
    for entry in os.scandir('/proc'):
        if entry.name.isdigit() and (read_text(f'/proc/{entry.name}/comm') or '').strip() == comm:
            found.append(int(entry.name))
    return found


def parse_memory(value):
    parts = value.split()
    if not parts:
        return 0
    scale = {'KiB': 1024, 'MiB': MIB, 'GiB': 1024 * MIB}.get(parts[1] if len(parts) > 1 else '', 1)
    return int(parts[0]) * scale


def drm_clients(pid):
    """drm-client-id -> {busy_ns, total, shared} for one process."""
    clients = {}
    try:
        fds = os.listdir(f'/proc/{pid}/fd')
    except OSError:
        return clients
    for fd in fds:
        try:
            if not os.readlink(f'/proc/{pid}/fd/{fd}').startswith('/dev/dri/'):
                continue
        except OSError:
            continue
        text = read_text(f'/proc/{pid}/fdinfo/{fd}')
        if not text or 'drm-client-id:' not in text:
            continue
        fields = dict(line.split(':', 1) for line in text.splitlines() if ':' in line)
        client = fields['drm-client-id'].strip()
        if client in clients:
            continue
        clients[client] = {
            'busy_ns': int(fields.get('drm-engine-gpu', '0 ns').split()[0]),
            'total': parse_memory(fields.get('drm-total-memory', '0')),
            'shared': parse_memory(fields.get('drm-shared-memory', '0')),
        }
    return clients


def process_snapshot(pid):
    stat = read_text(f'/proc/{pid}/stat')
    rollup = read_text(f'/proc/{pid}/smaps_rollup')
    if stat is None or rollup is None:
        return None
    fields = stat.rsplit(')', 1)[1].split()
    memory = {}
    for line in rollup.splitlines():
        key, _, value = line.partition(':')
        if key in ('Rss', 'Pss'):
            memory[key] = int(value.split()[0]) * 1024
    return {'ticks': int(fields[11]) + int(fields[12]), 'rss': memory.get('Rss', 0), 'pss': memory.get('Pss', 0),
            'clients': drm_clients(pid)}


def system_snapshot():
    cpu = [int(v) for v in read_text('/proc/stat').splitlines()[0].split()[1:]]
    meminfo = {}
    for line in read_text('/proc/meminfo').splitlines():
        key, _, value = line.partition(':')
        meminfo[key] = int(value.split()[0]) * 1024
    clients = {}
    for entry in os.scandir('/proc'):
        if entry.name.isdigit():
            for client, values in drm_clients(int(entry.name)).items():
                clients.setdefault(client, values)
    freq = None
    for device in DEVFREQ.glob('*gpu*'):
        value = read_text(device / 'cur_freq')
        if value:
            freq = int(value) / 1e6
    return {'idle': cpu[3] + cpu[4], 'total': sum(cpu[:8]), 'ram_used': meminfo['MemTotal'] - meminfo['MemAvailable'],
            'clients': clients, 'freq_mhz': freq}


def snapshot(pids):
    return {'time': time.monotonic(), 'system': system_snapshot(),
            'processes': {name: process_snapshot(pid) if pid else None for name, pid in pids.items()}}


def busy_percent(before, after, seconds):
    shared = before.keys() & after.keys()
    return 100 * sum(max(0, after[c]['busy_ns'] - before[c]['busy_ns']) for c in shared) / (seconds * 1e9)


def interval(before, after):
    seconds = after['time'] - before['time']
    a, b = before['system'], after['system']
    total = b['total'] - a['total']
    row = {
        'system_cpu': 100 * (1 - (b['idle'] - a['idle']) / total) if total else 0,
        'ram_used': b['ram_used'],
        'total_gpu_busy': busy_percent(a['clients'], b['clients'], seconds),
        'freq_mhz': b['freq_mhz'],
    }
    for name, now in after['processes'].items():
        then = before['processes'].get(name)
        if not now or not then:
            continue
        row[name] = {
            'cpu': 100 * (now['ticks'] - then['ticks']) / CLK_TCK / seconds,
            'rss': now['rss'], 'pss': now['pss'],
            'gpu_mem': sum(c['total'] for c in now['clients'].values()),
            'gpu_private': sum(c['total'] - c['shared'] for c in now['clients'].values()),
            'gpu_busy': busy_percent(then['clients'], now['clients'], seconds),
        }
    return row


class StatsPoller(threading.Thread):
    def __init__(self, cli, period=2.0):
        super().__init__(daemon=True)
        self.cli, self.period, self.replies = cli, period, []
        self.halt = threading.Event()

    def run(self):
        while not self.halt.is_set():
            started = time.monotonic()
            try:
                code, reply = self.cli('stats', timeout=10, check=False)
                if code == 0:
                    self.replies.append(reply['result'])
            except (CliError, subprocess.TimeoutExpired):
                pass
            self.halt.wait(max(0.0, self.period - (time.monotonic() - started)))


def profile_lines(since):
    done = subprocess.run(['journalctl', '--user', '-u', 'pocketspace.service', '--since', f'@{since:.0f}',
                           '-o', 'cat', '--no-pager'], capture_output=True, text=True)
    rows = []
    for line in done.stdout.splitlines():
        if line.startswith('[profile]'):
            rows.append({k: float(v) for k, v in (part.split('=') for part in line.split()[1:])})
    return rows


def capture(name, label, cli, engine_running, settle, samples):
    print(f'[{label}] settling {settle} s, then sampling {samples} s', flush=True)
    time.sleep(settle)
    pids = {'compositor': (pids_by_comm(COMPOSITOR_COMM) or [None])[0],
            'engine': (pids_by_comm(ENGINE_COMM) or [None])[0] if engine_running else None}
    poller = StatsPoller(cli) if engine_running else None
    wall_start = time.time()
    if poller:
        poller.start()
    rows, previous = [], snapshot(pids)
    deadline = previous['time']
    for _ in range(samples):
        deadline += 1.0
        time.sleep(max(0.0, deadline - time.monotonic()))
        current = snapshot(pids)
        rows.append(interval(previous, current))
        previous = current
    if poller:
        poller.halt.set()
        poller.join()
    return {'name': name, 'label': label, 'started': wall_start, 'samples': rows, 'stats': poller.replies if poller else [],
            'profile': profile_lines(wall_start) if engine_running else []}


# ---- engine and script control ---------------------------------------------------

def running_scripts(cli):
    _, reply = cli('list')
    return [i for i in reply['result']['instances'] if i['instanceId'] != 0 and i['state'] == 'running']


def start_demos(cli, names, catalog):
    started = []
    for name in names:
        _, reply = cli('start', f'{catalog}/{name}.js')
        started.append(reply['result'])
        print(f'  started {catalog}/{name}.js as instance {reply["result"]["instanceId"]}', flush=True)
    return started


def stop_demos(cli, instances):
    for instance in instances:
        cli('stop', str(instance['instanceId']), '--generation', str(instance['generation']), check=False)
    instances.clear()


def stop_engine():
    subprocess.run(['systemctl', '--user', 'stop', 'pocketspace.service'], check=False)
    for _ in range(50):
        if not pids_by_comm(ENGINE_COMM):
            return True
        time.sleep(0.2)
    return False


# ---- report ---------------------------------------------------------------------

def mean(values):
    values = [v for v in values if v is not None]
    return statistics.fmean(values) if values else None


def summarize(result, idle_ram):
    rows = result['samples']
    summary = {'name': result['name'], 'label': result['label']}
    for key in ('system_cpu', 'ram_used', 'total_gpu_busy', 'freq_mhz'):
        summary[key] = mean(r[key] for r in rows)
    for process in ('engine', 'compositor'):
        values = [r[process] for r in rows if process in r]
        if values:
            summary[process] = {k: mean(v[k] for v in values) for k in values[0]}
    fps = [s['fps'] for s in result['stats'] if s.get('fps') is not None]
    summary['fps'] = (mean(fps), min(fps), max(fps)) if fps else None
    last = result['stats'][-1] if result['stats'] else None
    summary['gpu_estimate'] = last.get('gpuEstimateBytes') if last else None
    callbacks, worst = [], []
    for stats in result['stats']:
        running = [i for i in stats['instances'] if i['instanceId'] != 0 and i['callbackMsPerSecond'] is not None]
        if running:
            callbacks.append(sum(i['callbackMsPerSecond'] for i in running))
            worst.append(max(i['worstCallbackMs'] for i in running))
    summary['callbacks'] = (mean(callbacks), max(worst)) if callbacks else None
    summary['profile'] = {k: mean(p[k] for p in result['profile']) for k in result['profile'][0]} if result['profile'] else None
    summary['ram_delta'] = summary['ram_used'] - idle_ram if idle_ram is not None else None
    summary['instances'] = [i for i in last['instances'] if i['instanceId'] != 0 and i['state'] == 'running'] if last else []
    return summary


def fmt(value, spec, suffix=''):
    return '—' if value is None else f'{value:{spec}}{suffix}'


REPEAT_METRICS = [
    ('FPS', lambda s: s['fps'][0] if s['fps'] else None, '.1f', ''),
    ('Engine CPU', lambda s: s.get('engine', {}).get('cpu'), '.1f', '%'),
    ('Engine GPU busy', lambda s: s.get('engine', {}).get('gpu_busy'), '.1f', '%'),
    ('Compositor GPU busy', lambda s: s.get('compositor', {}).get('gpu_busy'), '.1f', '%'),
    ('Total GPU busy', lambda s: s['total_gpu_busy'], '.0f', '%'),
    ('GPU wait ms', lambda s: s['profile']['gpu_wait_ms'] if s['profile'] else None, '.2f', ''),
]


def repeats_table(summaries):
    """Median and range across repeats: a change is real only if it moves the median beyond the range."""
    names = list(dict.fromkeys(s['name'] for s in summaries))
    lines = ['## Across repeats', '', 'Median (min–max) of each capture\'s average.', '',
             '| Capture | Runs | ' + ' | '.join(m[0] for m in REPEAT_METRICS) + ' |',
             '|' + '---|' * (len(REPEAT_METRICS) + 2)]
    for name in names:
        group = [s for s in summaries if s['name'] == name]
        cells = []
        for _, get, spec, suffix in REPEAT_METRICS:
            values = [v for v in map(get, group) if v is not None]
            cells.append(f'{statistics.median(values):{spec}}{suffix} ({min(values):{spec}}–{max(values):{spec}})'
                         if values else '—')
        lines.append(f'| {name} | {len(group)} | ' + ' | '.join(cells) + ' |')
    return lines + ['']


def report(summaries, meta):
    lines = ['# Demo benchmark', '',
             f'Captured {meta["date"]} at commit `{meta["commit"]}`{" (with uncommitted changes)" if meta["dirty"] else ""}: '
             f'{meta["settle"]} s settle, {meta["samples"]} one-second samples, `stats` every 2 s, '
             f'{meta["repeat"]} run(s) of each engine capture, scripts from `{meta["catalog"]}/`.', '',
             'Catalog demos with interactive panels capture pointer input while they run. '
             'The sampler itself uses about 5% of one core, included in System CPU.', '',
             '| Capture | FPS (min–max) | Engine CPU | Engine RSS (PSS) | Engine GPU mem (private) | GPU est. | '
             'Engine GPU busy | Compositor CPU | Compositor GPU mem | Compositor GPU busy | Total GPU busy @ MHz | '
             'System CPU | System RAM Δ | Callback ms/s (worst) | GPU wait / submit ms |',
             '|' + '---|' * 15]
    for s in summaries:
        e, c = s.get('engine', {}), s.get('compositor', {})
        fps = f'{s["fps"][0]:.1f} ({s["fps"][1]:.0f}–{s["fps"][2]:.0f})' if s['fps'] else '—'
        mem = (f'{e["rss"] / MIB:.0f} ({e["pss"] / MIB:.0f}) MiB' if e else '—')
        gpu = (f'{e["gpu_mem"] / MIB:.0f} ({e["gpu_private"] / MIB:.0f}) MiB' if e else '—')
        est = fmt(s['gpu_estimate'] / MIB if s['gpu_estimate'] is not None else None, '.1f', ' MiB')
        cb = f'{s["callbacks"][0]:.2f} ({s["callbacks"][1]:.2f})' if s['callbacks'] else '—'
        prof = f'{s["profile"]["gpu_wait_ms"]:.2f} / {s["profile"]["render_submit_ms"]:.2f}' if s['profile'] else '—'
        lines.append(f'| {s["label"]} | {fps} | {fmt(e.get("cpu"), ".1f", "%")} | {mem} | {gpu} | {est} | '
                     f'{fmt(e.get("gpu_busy"), ".1f", "%")} | {fmt(c.get("cpu"), ".1f", "%")} | '
                     f'{fmt(c["gpu_mem"] / MIB if c else None, ".0f", " MiB")} | {fmt(c.get("gpu_busy"), ".1f", "%")} | '
                     f'{s["total_gpu_busy"]:.0f}% @ {fmt(s["freq_mhz"], ".0f")} | {s["system_cpu"]:.0f}% | '
                     f'{fmt(s["ram_delta"] / MIB if s["ram_delta"] is not None else None, "+.0f", " MiB")} | {cb} | {prof} |')
    lines += ['', 'System RAM Δ is relative to the idle engine (or the baseline when idle was not captured) and '
              'includes background applications.', '']
    if meta['repeat'] > 1:
        lines += repeats_table(summaries)
    for s in summaries:
        if not s['instances']:
            continue
        lines += [f'## {s["label"]} scripts', '',
                  '| Script | JS heap | Objects | Panels | Assets | Bodies | GPU (est.) | Callback ms/s | Worst ms |',
                  '|---|---|---|---|---|---|---|---|---|']
        for i in s['instances']:
            lines.append(f'| {i["name"]} | {i["heapBytes"] / 1024:.1f} KiB | {i["objects"]} | {i["panels"]} | '
                         f'{i["assets"]} | {i["bodies"]} | {fmt(i["gpuEstimateBytes"] / MIB if i["gpuEstimateBytes"] is not None else None, ".1f", " MiB")} | '
                         f'{fmt(i["callbackMsPerSecond"], ".2f")} | {fmt(i["worstCallbackMs"], ".2f")} |')
        lines.append('')
    return '\n'.join(lines)


# ---- main -----------------------------------------------------------------------

def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--captures', default=','.join(CAPTURES), help=f'comma-separated subset of {",".join(CAPTURES)}')
    parser.add_argument('--demos', default=','.join(DEMOS), help='comma-separated demo names (default: all eight)')
    parser.add_argument('--settle', type=int, default=15, help='seconds to wait before sampling (default 15)')
    parser.add_argument('--samples', type=int, default=30, help='one-second samples per capture (default 30)')
    parser.add_argument('--catalog', default='demos', help='catalog folder holding the demo scripts (default demos)')
    parser.add_argument('--gap', type=int, default=8, help='seconds after stopping a demo before the next (default 8)')
    parser.add_argument('--cli', default=str(ROOT / 'build/pocketspace-cli'), help='pocketspace-cli path')
    parser.add_argument('--out', help='output directory (default build/benchmarks/<timestamp>)')
    parser.add_argument('--repeat', type=int, default=1, help='runs of the engine captures, for median and range (default 1)')
    parser.add_argument('--yes', action='store_true', help='start without waiting for Enter (the wearer is ready)')
    args = parser.parse_args()

    captures = args.captures.split(',')
    demos = args.demos.split(',')
    unknown = [c for c in captures if c not in CAPTURES] + [d for d in demos if d not in DEMOS]
    if unknown:
        parser.error(f'unknown capture or demo: {", ".join(unknown)}')
    if not pids_by_comm(COMPOSITOR_COMM):
        sys.exit('SteamVR is not running; start it and keep the headset active.')
    cli = Cli(args.cli)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out = pathlib.Path(args.out) if args.out else ROOT / 'build/benchmarks' / stamp
    meta = {'date': datetime.datetime.now().isoformat(timespec='seconds'), 'commit': git('rev-parse', '--short', 'HEAD'),
            'dirty': bool(git('status', '--porcelain', '--untracked-files=no')), 'settle': args.settle,
            'samples': args.samples, 'catalog': args.catalog, 'repeat': args.repeat, 'captures': captures, 'demos': demos}

    engine_up = cli('list', check=False)[0] == 0
    if engine_up and running_scripts(cli):
        sys.exit('Scripts are running: ' + ', '.join(i['name'] for i in running_scripts(cli)) +
                 '. Stop them before benchmarking.')
    if not args.yes:
        if not sys.stdin.isatty():
            sys.exit('No terminal to confirm on: pass --yes once the wearer is ready.')
        input('Wear the headset at the start spot and use it as you would for the comparison. Press Enter to start.')

    results, started_engine, live = [], False, []
    try:
        if 'baseline' in captures:
            if engine_up:
                print('[baseline] skipped: the engine was already running', flush=True)
            else:
                results.append(capture('baseline', 'baseline', cli, False, args.settle, args.samples))
        if not engine_up and set(captures) - {'baseline'}:
            _, reply = cli('start-engine', timeout=60)
            started_engine = reply['result']['started']
        for run in range(1, args.repeat + 1):
            label = (lambda name: f'{name} #{run}') if args.repeat > 1 else (lambda name: name)
            if 'idle' in captures:
                results.append(capture('idle', label('idle'), cli, True, args.settle, args.samples))
            if 'each' in captures:
                for name in demos:
                    live += start_demos(cli, [name], args.catalog)
                    results.append(capture(name, label(name), cli, True, args.settle, args.samples))
                    stop_demos(cli, live)
                    time.sleep(args.gap)
            if 'all' in captures:
                live += start_demos(cli, demos, args.catalog)
                results.append(capture('all', label('all'), cli, True, args.settle, args.samples))
                stop_demos(cli, live)
                time.sleep(args.gap)
            if 'after' in captures:
                results.append(capture('after', label('after'), cli, True, args.settle, args.samples))
    finally:
        if live:
            stop_demos(cli, live)
        if started_engine and not stop_engine():
            print('warning: the engine did not exit after stopping pocketspace.service', file=sys.stderr)

    reference = next((r for r in results if r['label'] in ('idle', 'idle #1')), None) or next(
        (r for r in results if r['name'] == 'baseline'), None)
    idle_ram = mean(s['ram_used'] for s in reference['samples']) if reference else None
    text = report([summarize(r, idle_ram) for r in results], meta)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'report.md').write_text(text + '\n')
    (out / 'samples.json').write_text(json.dumps({'meta': meta, 'captures': results}, indent=1) + '\n')
    print('\n' + text + f'\nWrote {out / "report.md"} and {out / "samples.json"}')


if __name__ == '__main__':
    main()
