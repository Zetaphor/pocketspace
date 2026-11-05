#!/usr/bin/env python3
"""Bounded two-script soak of the production engine, without input capture.

Starts build/pocketspace through run-pocketspace.sh with tests/scenes as the catalog
and a throwaway settings file, starts two display-only scripts over the control
socket, reloads them in turn, and samples `stats` every 30 seconds. At the end it
stops both scripts, sends SIGTERM, and checks that the engine exited cleanly and
removed its control socket. Needs SteamVR running and the headset awake; it never
starts SteamVR. Writes samples.json and summary.md to --output.

  python3 tests/multi-script-soak.py --output /tmp/soak --minutes 10
"""
import argparse, json, os, signal, socket, statistics, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOCKET = os.path.join(os.environ.get('XDG_RUNTIME_DIR', f'/tmp/pocketspace-{os.getuid()}'), 'pocketspace/control.sock')
SCRIPTS = ['event-bus-peer.js', 'tracking-smoke.js']
counter = 0


class Link:
    def __init__(self):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(10)
        self.sock.connect(SOCKET)
        self.lines = self.sock.makefile('r')

    def call(self, op, **fields):
        global counter
        counter += 1
        request = {'v': 1, 'id': f'soak-{os.getpid()}-{counter}', 'op': op, **fields}
        self.sock.sendall((json.dumps(request) + '\n').encode())
        while True:
            reply = json.loads(self.lines.readline())
            if reply.get('id') == request['id']:
                if not reply['ok']:
                    raise RuntimeError(f'{op} failed: {reply["error"]}')
                return reply['result']


def wait_for(condition, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        if condition():
            return
        time.sleep(.2)
    raise RuntimeError(f'timed out waiting for {what}')


def answering():
    try:
        Link().call('list')
        return True
    except (OSError, ValueError, RuntimeError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--minutes', type=float, default=10)
    parser.add_argument('--reload-seconds', type=float, default=60, help='reload one script, alternating, this often')
    args = parser.parse_args()
    if args.minutes < 2:
        parser.error('run at least two minutes')
    if subprocess.run(['pgrep', '-x', 'pocketspace'], capture_output=True).returncode == 0 or os.path.exists(SOCKET) and answering():
        parser.error('an engine is already running')
    args.output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as temporary:
        settings = Path(temporary) / 'settings.json'
        settings.write_text('{"version": 1, "autostart": [], "autoRestart": false, "engineAutostart": false}\n')
        log = (args.output / 'engine.log').open('w')
        engine = subprocess.Popen([str(ROOT / 'run-pocketspace.sh'), '--scripts-dir', str(ROOT / 'tests/scenes'), '--settings', str(settings)],
                                  cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(answering, 30, 'the control socket')
            link, watcher = Link(), Link()
            watcher.call('watch')
            watcher.sock.setblocking(False)
            instances = {}
            for name in SCRIPTS:
                started = link.call('start', name=name)
                assert started['state'] == 'running', started
                instances[name] = started
            samples, reloads, failures, events = [], 0, [], 0
            begin = time.time()
            next_sample, next_reload, turn = begin + 30, begin + args.reload_seconds, 0
            end = begin + args.minutes * 60
            pending = ''
            while time.time() < end:
                if engine.poll() is not None:
                    raise RuntimeError(f'engine exited early with {engine.returncode}')
                try:
                    pending += watcher.sock.recv(1 << 20).decode()
                except BlockingIOError:
                    pass
                while '\n' in pending:
                    line, pending = pending.split('\n', 1)
                    event = json.loads(line)
                    events += 1
                    if event.get('event') == 'error' or event.get('event') == 'status' and event['instance']['state'] != 'running':
                        failures.append(event)
                now = time.time()
                if now >= next_reload:
                    name = SCRIPTS[turn % len(SCRIPTS)]
                    current = instances[name]
                    reloaded = link.call('reload', instanceId=current['instanceId'], generation=current['generation'])
                    assert reloaded['generation'] == current['generation'] + 1, reloaded
                    instances[name] = reloaded
                    reloads += 1
                    turn += 1
                    next_reload += args.reload_seconds
                if now >= next_sample:
                    stats = link.call('stats')
                    samples.append({'seconds': round(now - begin, 1), **stats})
                    next_sample += 30
                time.sleep(.1)
            for name, current in instances.items():
                link.call('stop', instanceId=current['instanceId'], generation=current['generation'])
        finally:
            if engine.poll() is None:
                engine.send_signal(signal.SIGTERM)
            try:
                code = engine.wait(timeout=20)
            except subprocess.TimeoutExpired:
                engine.kill()
                code = 'killed after SIGTERM timeout'
            log.close()

    socket_left = os.path.exists(SOCKET)
    still_running = subprocess.run(['pgrep', '-x', 'pocketspace'], capture_output=True).returncode == 0
    (args.output / 'samples.json').write_text(json.dumps(samples, indent=1) + '\n')

    def series(field):
        return [s[field] for s in samples if s.get(field) is not None]

    fps, cpu, ram = series('fps'), series('cpuPercent'), series('ramBytes')
    heaps = {name: [i['heapBytes'] for s in samples for i in s['instances'] if i['name'] == name and i['heapBytes'] is not None] for name in SCRIPTS}
    lines = [
        f'# Two-script soak',
        '',
        f'{args.minutes:g} minutes, {len(samples)} samples, {reloads} reloads, {events} watch events.',
        f'Scripts: {", ".join(SCRIPTS)} from tests/scenes; no input capture.',
        '',
        f'- FPS: min {min(fps):.1f}, mean {statistics.mean(fps):.1f}, max {max(fps):.1f}' if fps else '- FPS: no samples',
        f'- CPU: mean {statistics.mean(cpu):.1f}%, max {max(cpu):.1f}%' if cpu else '- CPU: no samples',
        f'- RSS: first {ram[0] / 1048576:.1f} MiB, last {ram[-1] / 1048576:.1f} MiB, max {max(ram) / 1048576:.1f} MiB' if ram else '- RSS: no samples',
    ]
    for name, values in heaps.items():
        if values:
            lines.append(f'- {name} heap: first {values[0] / 1024:.0f} KiB, last {values[-1] / 1024:.0f} KiB, max {max(values) / 1024:.0f} KiB')
    lines += [
        f'- Script failures: {len(failures)}',
        f'- Engine exit: {code}; control socket left behind: {socket_left}; engine still running: {still_running}',
    ]
    summary = '\n'.join(lines) + '\n'
    (args.output / 'summary.md').write_text(summary)
    print(summary)
    passed = not failures and code == 0 and not socket_left and not still_running and samples
    print('PASS' if passed else 'FAIL')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
