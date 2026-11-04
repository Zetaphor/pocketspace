#!/usr/bin/env python3
"""Build regression targets together, then run them one at a time with a summary.

Regressions share the GPU and memory with the SteamVR compositor on the headset.
Running them concurrently destabilized a live session, so they stay serial.
"""
import argparse
from pathlib import Path
import os
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
# Measurements or fixtures with their own run instructions in tests/README.md.
EXCLUDED = {'physics-performance-regression'}
# These generate their fixtures in a wrapper script, which receives the binary.
WRAPPERS = {'morph-regression': 'tests/morph-loader-regression.py',
            'gpu-skinning-regression': 'tests/skinning-loader-regression.py'}


def discover():
    text = '\n'.join(path.read_text() for path in [ROOT / 'CMakeLists.txt', *sorted((ROOT / 'cmake').glob('*.cmake'))])
    # Targets are declared one per add_executable or in foreach lists.
    targets = set(re.findall(r'\b([\w-]+-regression)\b', text)) - EXCLUDED
    return sorted(targets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='*', help='regression targets; default is every target')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-test')
    parser.add_argument('--timeout', type=int, default=240, help='seconds allowed per regression')
    args = parser.parse_args()
    targets = args.targets or discover()
    if not (args.build_dir / 'CMakeCache.txt').exists():
        subprocess.run(['cmake', '-S', str(ROOT), '-B', str(args.build_dir), '-DSF_FAST_TESTS=ON'], check=True)

    started = time.monotonic()
    build = subprocess.run(['cmake', '--build', str(args.build_dir), '-j', '1', '--target', *targets])
    built = time.monotonic()
    print(f'BUILD {built - started:.0f}s exit={build.returncode}', flush=True)
    if build.returncode:
        return build.returncode

    environment = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11',
                   'LD_LIBRARY_PATH': '/opt/steamvr/bin/linuxarm64'}
    logs = args.build_dir / 'regression-logs'
    logs.mkdir(exist_ok=True)

    def run(target):
        begin = time.monotonic()
        with (logs / f'{target}.log').open('w') as log:
            try:
                binary = str(args.build_dir.resolve() / target)
                command = [sys.executable, WRAPPERS[target], binary] if target in WRAPPERS else [binary]
                code = subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                                      stderr=subprocess.STDOUT, timeout=args.timeout).returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
        print(f'{"ok  " if code == 0 else "FAIL"} {target} {time.monotonic() - begin:.1f}s'
              + ('' if code == 0 else f' exit={code} log={logs / (target + ".log")}'), flush=True)
        return code == 0

    results = [run(target) for target in targets]
    print(f'TESTS {time.monotonic() - built:.0f}s passed={sum(results)}/{len(results)} '
          f'total={time.monotonic() - started:.0f}s', flush=True)
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
