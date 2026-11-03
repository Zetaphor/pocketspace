#!/usr/bin/env python3
"""Drive a running engine through its control socket, or check the user service.

Two-script check (engine started with --scripts-dir tests/scenes):
  python3 tests/control-live-check.py
Service check (after tools/install-user-service.sh; starts and stops the service):
  python3 tests/control-live-check.py --service
Neither path starts SteamVR; both need it already running. Scenes are display-only.
"""
import argparse, json, os, socket, subprocess, sys, time

SOCKET = os.path.join(os.environ.get('XDG_RUNTIME_DIR', f'/tmp/pocketspace-{os.getuid()}'), 'pocketspace/control.sock')
counter = 0


class Link:
    def __init__(self, path=SOCKET):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(5)
        self.sock.connect(path)
        self.lines = self.sock.makefile('r')

    def call(self, op, **fields):
        global counter
        counter += 1
        request = {'v': 1, 'id': f'live-{os.getpid()}-{counter}', 'op': op, **fields}
        self.sock.sendall((json.dumps(request) + '\n').encode())
        while True:
            reply = json.loads(self.lines.readline())
            if reply.get('id') == request['id']:
                return reply
            if 'event' not in reply:
                raise RuntimeError(f'unexpected reply {reply}')

    def ok(self, op, **fields):
        reply = self.call(op, **fields)
        if not reply['ok']:
            raise RuntimeError(f'{op} failed: {reply["error"]}')
        return reply['result']


def wait_for_socket(present, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        try:
            Link().ok('list')
            if present:
                return
        except OSError:
            if not present and not os.path.exists(SOCKET):
                return
        time.sleep(.2)
    raise RuntimeError(f'socket did not {"appear" if present else "go away"}')


def scripts():
    link, watcher = Link(), Link()
    watcher.ok('watch')
    names = ['event-bus-peer.js', 'crash-on-button.js']
    started = [link.ok('start', name=name) for name in names]
    assert all(s['state'] == 'running' for s in started), started
    assert link.call('start', name=names[0])['error']['code'] == 'conflict'
    first, second = started
    reloaded = link.ok('reload', instanceId=first['instanceId'], generation=first['generation'])
    assert reloaded['generation'] == first['generation'] + 1
    stale = link.call('eval', instanceId=first['instanceId'], generation=first['generation'], code='1')
    assert stale['error']['code'] == 'stale_generation'
    value = link.ok('eval', instanceId=second['instanceId'], generation=second['generation'], code='typeof update')
    assert value['result'] == '"function"', value
    scratch = link.ok('eval', code="print('from scratch'); 6 * 7")
    assert scratch['result'] == '42' and scratch['output'] == 'from scratch\n', scratch
    assert link.ok('publish', topic='space.liveCheck', payload={'ok': True})['sequence']
    for instance, generation in ((reloaded['instanceId'], reloaded['generation']), (second['instanceId'], second['generation'])):
        assert link.ok('stop', instanceId=instance, generation=generation)['state'] == 'stopped'
    states = {i['instanceId']: i['state'] for i in link.ok('list')['instances']}
    assert states[0] == 'running' and states[first['instanceId']] == 'stopped' and states[second['instanceId']] == 'stopped', states
    events = []
    watcher.sock.settimeout(1)
    try:
        while len(events) < 5:
            events.append(json.loads(watcher.lines.readline()))
    except (socket.timeout, ValueError):
        pass
    assert any(e.get('event') == 'status' for e in events), events
    print(f'PASS live control: started, reloaded, evaluated, published and stopped two scripts; {len(events)} watch events')


def service():
    def systemctl(*args):
        subprocess.run(['systemctl', '--user', *args], check=True)
    systemctl('start', 'pocketspace.service')
    wait_for_socket(True)
    before = subprocess.run(['systemctl', '--user', 'show', '-p', 'MainPID', '--value', 'pocketspace.service'], capture_output=True, text=True).stdout.strip()
    systemctl('restart', 'pocketspace.service')
    wait_for_socket(True)
    after = subprocess.run(['systemctl', '--user', 'show', '-p', 'MainPID', '--value', 'pocketspace.service'], capture_output=True, text=True).stdout.strip()
    assert before != after and after != '0', (before, after)
    assert Link().ok('list')['instances'][0]['name'] == 'scratch'
    systemctl('stop', 'pocketspace.service')
    wait_for_socket(False)
    print(f'PASS service: started, restarted (pid {before} -> {after}) and stopped; socket removed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--service', action='store_true')
    args = parser.parse_args()
    try:
        service() if args.service else scripts()
    except Exception as error:
        print(f'FAIL {error}', file=sys.stderr)
        sys.exit(1)
