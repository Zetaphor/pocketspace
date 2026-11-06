#!/usr/bin/env python3
"""Drive pocketspace-cli against the cli-regression control host, without VR.

Usage: cli-regression.py <build-dir>/cli-regression
pocketspace-cli is taken from the same build directory. Covers the noninteractive
harness path (JSON replies, exit codes, a bounded watch with reconnect markers,
uncertain outcomes, generations, entering and leaving a script) and an interactive
REPL session on a pseudo-terminal.
"""
import json, os, pty, select, signal, subprocess, sys, tempfile, time
from pathlib import Path

HOST = Path(sys.argv[1]).resolve()
CLI = HOST.parent / 'pocketspace-cli'
ROOT = Path(tempfile.mkdtemp(prefix='pocketspace-cli-'))
CATALOG, SOCKET, SETTINGS, STATE = ROOT / 'catalog', ROOT / 'run/control.sock', ROOT / 'settings.json', ROOT / 'state'
host = None


def fail(message):
    raise AssertionError(message)


def write(name, text):
    (CATALOG / name).write_text(text)


def start_host():
    global host
    host = subprocess.Popen([str(HOST), str(CATALOG), str(SOCKET), str(SETTINGS), str(STATE)], stdout=subprocess.PIPE, text=True)
    for line in host.stdout:
        if 'listening' in line:
            return
    fail('control host did not start')


def stop_host():
    host.send_signal(signal.SIGTERM)
    host.wait(10)


def cli(*args, code=0, timeout=20):
    """Runs one JSON command without a TTY; checks its exit code and returns the reply."""
    result = subprocess.run([str(CLI), '--socket', str(SOCKET), '--json', *args], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode != code:
        fail(f'{args}: exit {result.returncode}, expected {code}\nstdout: {result.stdout}\nstderr: {result.stderr}')
    if '\x1b' in result.stdout:
        fail(f'{args}: terminal control codes in JSON output')
    lines = result.stdout.splitlines()
    if len(lines) != 1:
        fail(f'{args}: expected one JSON line, got {result.stdout!r}')
    reply = json.loads(lines[0])
    if reply.get('v') != 1:
        fail(f'{args}: unversioned reply {reply}')
    return reply


def result(*args, **kwargs):
    return cli(*args, **kwargs)['result']


def error(*args, code):
    reply = cli(*args, code=code)
    if reply['ok']:
        fail(f'{args}: expected failure')
    return reply['error']


def scratch(code):
    return result('eval', '--code', code)['result']


def instance(name):
    found = [i for i in result('list')['instances'] if i['name'] == name and i['state'] == 'running']
    if len(found) != 1:
        fail(f'expected one running {name}, got {found}')
    return found[0]


def until(check, seconds=5):
    end = time.time() + seconds
    while time.time() < end:
        if check():
            return
        time.sleep(.05)
    fail('condition not reached')


def noninteractive():
    # Offline: a clear code before the engine runs.
    assert error('list', code=3)['code'] == 'offline'
    assert error('data', 'reset', 'a.js', code=3)['code'] == 'offline'
    start_host()
    assert result('catalog')['scripts'] == ['a.js', 'b.js', 'tools/c.js']
    assert [i['name'] for i in result('list')['instances']] == ['scratch']

    started = result('start', 'a.js')
    assert started['instanceId'] == 1 and started['generation'] == 1 and started['state'] == 'running'
    assert error('start', 'a.js', code=6)['code'] == 'conflict'
    assert error('start', 'missing.js', code=5)['code'] == 'not_found'
    assert result('status', '1')['name'] == 'a.js'
    assert error('status', '9', code=5)['code'] == 'not_found'

    # Usage errors are JSON too, with exit 2.
    assert error('status', code=2)['code'] == 'usage'
    assert error('frobnicate', code=2)['code'] == 'usage'
    assert error('publish', 'space.x', '{bad', code=2)['code'] == 'usage'

    # Eval: results, output, exceptions; a user script needs its current generation.
    evaluated = result('eval', '--code', "print('hi');globalThis.made=createObject(loadModel('assets/samples/Box/Box.glb'),0,0,0,.1);1+1")
    assert evaluated['result'] == '2' and evaluated['output'] == 'hi\n' and evaluated['instanceId'] == 0
    assert cli('eval', '--code', 'null.x', code=10)['result']['exception'].startswith('TypeError')
    source = ROOT / 'snippet.js'
    source.write_text('value * 10')
    assert result('eval', '--file', str(source), '--script', '1')['result'] == '10'
    assert result('eval', '--script', '1', '--code', 'value += 1')['result'] == '2'
    assert error('eval', '--script', '1', '--generation', '7', '--code', 'value', code=7)['generation'] == 1
    # A tight loop stops only its own scene.
    assert error('eval', '--script', '1', '--eval-timeout', '50', '--code', 'for(;;){}', code=11)['code'] == 'timeout'
    assert result('status', '1')['state'] == 'failed'
    assert scratch('typeof made') == '"number"'
    assert error('eval', '--eval-timeout', '50', '--code', 'for(;;){}', code=11)['code'] == 'timeout'
    assert scratch('typeof made') == '"undefined"'

    # Stop and reload look up the generation, or check the one given.
    a = result('start', 'a.js')
    assert error('reload', str(a['instanceId']), '--generation', '9', code=7)['generation'] == 1
    assert result('reload', str(a['instanceId']))['generation'] == 2
    write('a.js', "throw Error('EXPECTED_CLI_RELOAD')")
    failed = error('reload', str(a['instanceId']), code=9)
    assert failed['code'] == 'reload_failed' and failed['instance']['lastReload'] == 'failedKeptScene'
    write('a.js', 'globalThis.value = 1; function update() {}')

    # Scratch manages scripts through its scripts object; changes apply between frames.
    assert scratch("scripts.start('tools/c.js')") == 'undefined'
    until(lambda: any(i['name'] == 'tools/c.js' and i['state'] == 'running' for i in result('list')['instances']))
    c = instance('tools/c.js')
    scratch(f"scripts.stop({c['instanceId']}, {c['generation']})")
    until(lambda: result('status', str(c['instanceId']))['state'] == 'stopped')

    # Publish acts as scratch; scripts see it.
    assert isinstance(result('publish', 'space.ping', '{"n": 1}')['sequence'], (int, float))
    assert error('publish', 'engine.fake', code=8)['code'] == 'invalid'
    until(lambda: result('eval', '--script', str(a['instanceId']), '--code', 'globalThis.pings || 0')['result'] == '1')

    # An unanswered change has an unknown outcome; look it up rather than resend it.
    os.kill(host.pid, signal.SIGSTOP)
    try:
        lost = error('--timeout', '0.3', '--request-id', 'harness-start-b', 'start', 'b.js', code=4)
    finally:
        os.kill(host.pid, signal.SIGCONT)
    assert lost['code'] == 'unknown_outcome' and lost['requestId'] == 'harness-start-b'
    until(lambda: subprocess.run([str(CLI), '--socket', str(SOCKET), 'outcome', 'harness-start-b'], capture_output=True).returncode == 0)
    remembered = result('outcome', 'harness-start-b')['response']
    assert remembered['ok'] and remembered['result']['name'] == 'b.js'
    repeated = cli('--request-id', 'harness-start-b', 'start', 'b.js')
    assert repeated['duplicate'] is True and repeated['result']['instanceId'] == remembered['result']['instanceId']
    assert sum(i['name'] == 'b.js' for i in result('list')['instances']) == 1
    assert error('outcome', 'never-sent', code=5)['code'] == 'not_found'

    # Autostart edits go through the engine's settings.
    assert result('autostart', 'list')['autostart'] == []
    assert result('autostart', 'add', 'a.js')['settings']['autostart'] == ['a.js']
    assert error('autostart', 'add', 'a.js', code=6)['code'] == 'conflict'
    assert error('autostart', 'add', 'missing.js', code=5)['code'] == 'not_found'
    assert result('autostart', 'add', 'tools/c.js')['settings']['autostart'] == ['a.js', 'tools/c.js']
    assert result('autostart', 'remove', 'a.js')['settings']['autostart'] == ['tools/c.js']
    assert error('autostart', 'remove', 'a.js', code=5)['code'] == 'not_found'
    assert json.loads(SETTINGS.read_text())['autostart'] == ['tools/c.js']

    # Resources: engine totals once, live values per running script, none for stopped ones.
    stats = result('stats')
    running = [i for i in stats['instances'] if i['state'] == 'running']
    assert len(running) >= 2 and all(i['heapBytes'] > 0 and i['objects'] is not None for i in running), stats
    assert all(i['heapBytes'] is None for i in stats['instances'] if i['state'] != 'running')
    assert stats['vramBytes'] is None and 'dropped' in stats['bus']
    table = subprocess.run([str(CLI), '--socket', str(SOCKET), 'stats'], capture_output=True, text=True, timeout=10)
    assert table.returncode == 0 and 'JS HEAP' in table.stdout and 'unavailable' in table.stdout, table.stdout
    # Watchers get a resources event after each one-second sample.
    stream = subprocess.run([str(CLI), '--socket', str(SOCKET), '--json', 'watch', '--duration', '2.5'],
                            capture_output=True, text=True, timeout=10)
    samples = [e for e in map(json.loads, stream.stdout.splitlines()) if e['event'] == 'resources']
    assert samples and samples[-1]['sampleId'] > 0 and samples[-1]['instances'][0]['instanceId'] == 0, stream.stdout
    assert all(i['callbackMsPerSecond'] is not None for i in samples[-1]['instances'] if i['state'] == 'running'), samples[-1]
    scratch('globalThis.keep = 1')
    assert result('scratch', 'reset')['instanceId'] == 0
    assert scratch('typeof keep') == '"undefined"'

    # data reset deletes a script's saves; a running script reloads from its defaults.
    write('saver.js', "const n = loadScriptData('n') ?? 0; saveScriptData('n', n + 1); globalThis.count = n + 1; function update() {}")
    saver = str(result('start', 'saver.js')['instanceId'])
    result('reload', saver)
    assert result('eval', '--script', saver, '--code', 'count')['result'] == '2'
    assert (STATE / 'saver.js' / 'n.data.json').is_file()
    reset = result('data', 'reset', 'saver.js')
    assert reset['removed'] == 1 and reset['instance']['generation'] == 3, reset
    assert result('eval', '--script', saver, '--code', 'count')['result'] == '1'
    human = subprocess.run([str(CLI), '--socket', str(SOCKET), 'data', 'reset', 'saver.js'], capture_output=True, text=True, timeout=10)
    assert human.returncode == 0 and human.stdout == 'removed 1 saved file for saver.js; reloaded (generation 4)\n', human.stdout
    first = cli('--request-id', 'harness-reset', 'data', 'reset', 'saver.js')
    again = cli('--request-id', 'harness-reset', 'data', 'reset', 'saver.js')
    assert again['duplicate'] is True and again['result']['instance']['generation'] == first['result']['instance']['generation']
    result('stop', saver)
    stopped = result('data', 'reset', 'saver.js')
    assert stopped['removed'] == 1 and stopped['instance'] is None and not (STATE / 'saver.js' / 'n.data.json').exists()
    human = subprocess.run([str(CLI), '--socket', str(SOCKET), 'data', 'reset', 'saver.js'], capture_output=True, text=True, timeout=10)
    assert human.returncode == 0 and human.stdout == 'removed 0 saved files for saver.js\n', human.stdout
    assert error('data', 'reset', 'missing.js', code=5)['code'] == 'not_found'
    assert error('data', 'reset', code=2)['code'] == 'usage'
    assert error('data', 'clear', 'saver.js', code=2)['code'] == 'usage'
    (CATALOG / 'saver.js').unlink()

    # A bounded watch: one JSON object per line with sequence numbers.
    b = instance('b.js')
    # Resources samples interleave with status events, so read for a fixed time.
    watcher = subprocess.Popen([str(CLI), '--socket', str(SOCKET), '--json', 'watch', '--duration', '2'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(.5)
    result('reload', str(b['instanceId']))
    result('stop', str(b['instanceId']))
    out, _ = watcher.communicate(timeout=10)
    events = [json.loads(line) for line in out.splitlines()]
    status = [e for e in events if e['event'] == 'status']
    assert watcher.returncode == 0 and len(status) == 2, out
    assert [e['instance']['state'] for e in status] == ['running', 'stopped'] and status[0]['seq'] < status[1]['seq']
    assert all(earlier['seq'] < later['seq'] for earlier, later in zip(events, events[1:]))
    # --limit ends the stream after that many events.
    limited = subprocess.run([str(CLI), '--socket', str(SOCKET), '--json', 'watch', '--limit', '1'], capture_output=True, text=True, timeout=10)
    assert limited.returncode == 0 and len(limited.stdout.splitlines()) == 1, limited.stdout
    begin = time.time()
    quiet = subprocess.run([str(CLI), '--socket', str(SOCKET), '--json', 'watch', '--duration', '0.5'], capture_output=True, text=True, timeout=10)
    # With nothing changing, only the periodic resources samples arrive.
    assert quiet.returncode == 0 and time.time() - begin < 3
    assert all(json.loads(line)['event'] == 'resources' for line in quiet.stdout.splitlines()), quiet.stdout

    # A restarted engine: the stream marks the loss and the reconnect.
    watcher = subprocess.Popen([str(CLI), '--socket', str(SOCKET), '--json', 'watch', '--duration', '4'],
                               stdout=subprocess.PIPE, text=True)
    time.sleep(.5)
    stop_host()
    start_host()
    out, _ = watcher.communicate(timeout=15)
    kinds = [json.loads(line)['event'] for line in out.splitlines()]
    assert 'connectionLost' in kinds and 'reconnected' in kinds and kinds.index('connectionLost') < kinds.index('reconnected'), out
    result('start', 'a.js')

    # The REPL without a TTY: no prompts, enters and leaves a script, edits both scenes.
    session = subprocess.run([str(CLI), '--socket', str(SOCKET), 'repl'], input=(
        'globalThis.box = createObject(loadModel("assets/samples/Box/Box.glb"), 0, 0, 0, .1)\n'
        'function twice(x) {\n  return x * 2\n}\ntwice(21)\n'
        ':enter 1\n:where\nvalue = 7\n:leave\ntypeof value\n:enter 99\n'), capture_output=True, text=True, timeout=20)
    lines = session.stdout.splitlines()
    assert session.returncode == 0 and '>' not in session.stdout and '\x1b' not in session.stdout, session.stdout
    assert lines[0] == '1' and '42' in lines and 'entered 1 a.js, generation 1. Input now changes that live scene.' in lines, lines
    assert 'instance 1 a.js, generation 1' in lines and '7' in lines and lines[-2] == '"undefined"', lines
    assert lines[-1].startswith('cannot enter 99'), lines
    assert result('eval', '--script', '1', '--code', 'value')['result'] == '7'
    assert scratch('typeof box') == '"number"'


def interactive():
    """A REPL on a pseudo-terminal, as a person would use it."""
    master, slave = pty.openpty()
    repl = subprocess.Popen([str(CLI), '--socket', str(SOCKET), 'repl'], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    seen = ''

    def expect(text, seconds=10):
        nonlocal seen
        end = time.time() + seconds
        while time.time() < end:
            if text in seen:
                at = seen.index(text) + len(text)
                before, seen = seen[:at], seen[at:]
                return before
            ready, _, _ = select.select([master], [], [], .1)
            if ready:
                try:
                    seen += os.read(master, 65536).decode(errors='replace')
                except OSError:
                    break
        fail(f'REPL never printed {text!r}; got {seen!r}')

    def send(text):
        os.write(master, text.encode())

    expect('scratch> ')
    send("print('hello'); 6 * 7\r")
    shown = expect('scratch> ')
    assert 'hello' in shown and '42' in shown, shown
    send('[1,\r')
    expect('... ')
    send('2].length\r')
    assert '2' in expect('scratch> ')
    # Ctrl-R searches earlier input; Enter runs the match.
    send('\x12')
    expect('bck:')
    send('6 * 7')
    send('\r')
    shown = expect('scratch> ')
    assert 'hello' in shown and '42' in shown, shown
    send('globalThis.marker = createObject(loadModel("assets/samples/Box/Box.glb"), 0, 0, 0, .1)\r')
    expect('scratch> ')
    send(':enter 1\r')
    expect('1:a.js@1> ')
    send('value = 11\r')
    assert '11' in expect('1:a.js@1> ')
    # Another client reloads the entered script; the REPL returns to scratch.
    result('reload', '1')
    time.sleep(.3)
    send('\r')
    assert 'reloaded (generation 2); back in scratch' in expect('scratch> ')
    send('nope(\r')
    expect('... ')
    send('\r')
    assert 'Uncaught SyntaxError' in expect('scratch> ')
    send(':help\r')
    assert ':enter <id>' in expect('scratch> ')
    send('\x04')
    repl.wait(10)
    os.close(master)
    assert repl.returncode == 0
    # Scratch keeps what the session made.
    assert scratch('typeof marker') == '"number"'
    assert result('eval', '--script', '1', '--code', 'value')['result'] == '1'


def main():
    CATALOG.mkdir(parents=True)
    (CATALOG / 'tools').mkdir()
    write('a.js', "globalThis.value = 1; subscribe('space.ping', () => globalThis.pings = (globalThis.pings || 0) + 1); function update() {}")
    write('b.js', 'function update() {}')
    write('tools/c.js', 'function update() {}')
    try:
        noninteractive()
        interactive()
    finally:
        if host and host.poll() is None:
            stop_host()
        subprocess.run(['rm', '-rf', str(ROOT)])
    print('PASS cli: offline and usage errors, catalog/list/status/start/stop/reload with generations, eval results/output/'
          'exceptions/timeouts in scratch and a script, scratch scripts object, publish, unknown outcome lookup without '
          'resending, autostart edits, per-script stats and resources events, scratch reset, data reset, bounded watch with reconnect markers, noninteractive REPL '
          'enter/leave, interactive REPL with Ctrl-R history search on a pseudo-terminal')


if __name__ == '__main__':
    main()
