#!/usr/bin/env python3
"""Install, update and remove the demos with tools/install-user-service.sh.

Runs the real script under a temporary HOME with a stand-in systemctl, so the
user's services and catalog are untouched.
"""
import filecmp
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/install-user-service.sh'


def check(condition, message):
    if not condition:
        sys.exit(f'FAIL {message}')


with tempfile.TemporaryDirectory(prefix='sf-install-') as temp:
    home = Path(temp) / 'home'
    bin_dir = Path(temp) / 'bin'
    bin_dir.mkdir()
    calls = Path(temp) / 'systemctl.log'
    fake = bin_dir / 'systemctl'
    fake.write_text(f'#!/bin/sh\necho "$@" >> "{calls}"\n')
    fake.chmod(0o755)
    environment = {**os.environ, 'HOME': str(home), 'PATH': f'{bin_dir}:{os.environ["PATH"]}'}
    environment.pop('XDG_STATE_HOME', None)

    def install(*args):
        result = subprocess.run(['bash', str(SCRIPT), *args], env=environment,
                                capture_output=True, text=True)
        check(result.returncode == 0, f'install {args}: {result.stderr}')

    catalog = home / '.local/share/pocketspace/scripts'
    demos = catalog / 'demos'
    units = home / '.config/systemd/user'
    saves = home / '.local/state/pocketspace/scenes/demos%2Fclock.js'

    def matches_checkout():
        names = sorted(path.name for path in (ROOT / 'demos').iterdir())
        check(names and sorted(path.name for path in demos.iterdir()) == names,
              'installed demos match the checkout')
        for name in names:
            check(not (demos / name).is_symlink(), f'{name} is a copy, not a link')
            check(filecmp.cmp(ROOT / 'demos' / name, demos / name, shallow=False), f'{name} contents')

    # A fresh install creates the catalog, copies every demo and writes the units.
    install()
    matches_checkout()
    check(not demos.is_symlink(), 'demos folder is a copy')
    service = (units / 'pocketspace.service').read_text()
    check(f'WorkingDirectory={ROOT}' in service, 'engine runs in the checkout, where demo models resolve')
    check('enable pocketspace-dashboard.service' in calls.read_text(), 'dashboard enabled')

    # Reinstalling replaces edited and stray files, and keeps other scripts and saves.
    (catalog / 'my-scene.js').write_text('// mine\n')
    saves.mkdir(parents=True)
    (saves / 'clock.data.json').write_text('{}')
    first = sorted((ROOT / 'demos').iterdir())[0].name
    (demos / first).write_text('// edited\n')
    (demos / 'old-demo.js').write_text('// removed upstream\n')
    install()
    matches_checkout()
    check((catalog / 'my-scene.js').is_file() and (saves / 'clock.data.json').is_file(),
          'update keeps other scripts and saves')
    check(not (catalog / 'demos.tmp').exists(), 'no staging folder left behind')

    # Removal deletes the units and the installed demos only.
    install('--remove')
    check(not demos.exists(), 'demos removed')
    check(not (units / 'pocketspace.service').exists(), 'units removed')
    check((catalog / 'my-scene.js').is_file() and (saves / 'clock.data.json').is_file(),
          'removal keeps other scripts and saves')
    install('--remove')
print('PASS install demos: copy, update, stray files, other scripts and saves kept, removal')
