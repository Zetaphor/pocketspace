#!/usr/bin/env python3
"""Add Pocketspace to the Steam library as a VR shortcut with its library art.

The shortcut launches `build/pocketspace-dashboard --show`, which opens the
Pocketspace dashboard tab. The art comes from assets/branding: capsule, header,
hero, logo and icon. Running it again updates the existing shortcut.

Steam must be running with its CEF debugger on 127.0.0.1:8080 (SteamOS starts it
with -cef-enable-debugging). The change is made through Steam's own client API,
so it takes effect immediately and needs no Steam restart. Usage:

  python3 tools/install-steam-shortcut.py            add or update the shortcut
  python3 tools/install-steam-shortcut.py --remove   remove it
"""
import argparse, base64, glob, json, os, socket, struct, sys, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'Pocketspace'
EXE = ROOT / 'build/pocketspace-dashboard'
ART = ROOT / 'assets/branding'
# Steam's custom-artwork slots for SetCustomArtworkForApp.
ARTWORK = {0: 'library_capsule_600x900.png', 1: 'library_hero_3840x1240.png',
           2: 'library_logo_1280x720.png', 3: 'library_header_920x430.png'}
ICON = ART / 'shortcut_icon_512x512.png'


class Socket:
    """A minimal WebSocket client for the Chrome DevTools protocol."""
    def __init__(self, url):
        hostport, path = url[len('ws://'):].split('/', 1)
        host, port = hostport.split(':')
        self.sock = socket.create_connection((host, int(port)), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f'GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\n'
                           f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        reply = b''
        while b'\r\n\r\n' not in reply:
            reply += self.sock.recv(4096)
        if b' 101 ' not in reply.split(b'\r\n')[0]:
            raise SystemExit('Steam refused the debugger connection')
        self.pending = reply.split(b'\r\n\r\n', 1)[1]

    def read(self, count):
        while len(self.pending) < count:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise SystemExit('Steam closed the debugger connection')
            self.pending += chunk
        out, self.pending = self.pending[:count], self.pending[count:]
        return out

    def send(self, text):
        data, mask = text.encode(), os.urandom(4)
        n = len(data)
        length = bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n) if n < 65536 else bytes([0x80 | 127]) + struct.pack('>Q', n)
        self.sock.sendall(bytes([0x81]) + length + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def receive(self):
        message = b''
        while True:
            first, second = self.read(2)
            n = second & 0x7f
            if n == 126:
                n = struct.unpack('>H', self.read(2))[0]
            elif n == 127:
                n = struct.unpack('>Q', self.read(8))[0]
            message += self.read(n)
            if first & 0x80:
                return message.decode()


class Steam:
    def __init__(self):
        try:
            targets = json.load(urllib.request.urlopen('http://127.0.0.1:8080/json', timeout=5))
        except OSError:
            raise SystemExit('Steam is not running with its CEF debugger on 127.0.0.1:8080')
        url = next((t['webSocketDebuggerUrl'] for t in targets if t.get('title') == 'SharedJSContext'), None)
        if not url:
            raise SystemExit("Steam's SharedJSContext is not available")
        self.socket, self.counter = Socket(url), 0

    def run(self, expression):
        self.counter += 1
        self.socket.send(json.dumps({'id': self.counter, 'method': 'Runtime.evaluate',
                                     'params': {'expression': expression, 'awaitPromise': True, 'returnByValue': True}}))
        while True:
            reply = json.loads(self.socket.receive())
            if reply.get('id') == self.counter:
                result = reply['result']
                if 'exceptionDetails' in result:
                    raise SystemExit(f'Steam rejected a call: {result["exceptionDetails"]}')
                return result['result'].get('value')


def parse_vdf(data, i=0):
    """Steam's binary VDF, as used by shortcuts.vdf."""
    out = {}
    while True:
        kind = data[i]; i += 1
        if kind == 8:
            return out, i
        end = data.index(0, i); key = data[i:end].decode('utf-8', 'replace'); i = end + 1
        if kind == 0:
            value, i = parse_vdf(data, i)
        elif kind == 1:
            end = data.index(0, i); value = data[i:end].decode('utf-8', 'replace'); i = end + 1
        elif kind == 2:
            value = struct.unpack('<I', data[i:i + 4])[0]; i += 4
        else:
            raise ValueError(f'unknown VDF type {kind}')
        out[key] = value


def existing_shortcuts(steam):
    """App IDs of shortcuts in the signed-in account that launch the dashboard."""
    account = steam.run('App.m_CurrentUser.strAccountID') or steam.run('String(App.m_CurrentUser.strSteamID)')
    found = []
    for path in glob.glob(os.path.expanduser('~/.steam/steam/userdata/*/config/shortcuts.vdf')):
        account_id = Path(path).parts[-3]
        if account and str(account) not in (account_id, str(int(account_id) + 76561197960265728)):
            continue
        shortcuts = parse_vdf(Path(path).read_bytes())[0].get('shortcuts', {})
        for entry in shortcuts.values():
            if entry.get('Exe', '').strip('"') == str(EXE):
                found.append(entry['appid'])
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--remove', action='store_true', help='remove the Pocketspace shortcut')
    args = parser.parse_args()
    steam = Steam()
    current = existing_shortcuts(steam)
    if args.remove:
        for app in current:
            steam.run(f'SteamClient.Apps.RemoveShortcut({app})')
        print(f'Removed {len(current)} Pocketspace shortcut(s).')
        return 0
    for name in [*ARTWORK.values(), ICON.name]:
        if not (ART / name).is_file():
            raise SystemExit(f'missing {ART / name}')
    if not EXE.is_file():
        raise SystemExit(f'build the dashboard first: {EXE} is missing')
    if current:
        app = current[0]
        print(f'Updating the Pocketspace shortcut ({app}).')
    else:
        app = steam.run(f'SteamClient.Apps.AddShortcut({json.dumps(NAME)}, {json.dumps(str(EXE))}, {json.dumps(str(EXE.parent))}, "--show")')
        if not app:
            raise SystemExit('Steam did not create the shortcut')
        print(f'Added the Pocketspace shortcut ({app}).')
    steam.run(f'SteamClient.Apps.SetShortcutName({app}, {json.dumps(NAME)})')
    steam.run(f'SteamClient.Apps.SetShortcutExe({app}, {json.dumps(json.dumps(str(EXE)))})')
    steam.run(f'SteamClient.Apps.SetShortcutStartDir({app}, {json.dumps(json.dumps(str(EXE.parent)))})')
    steam.run(f'SteamClient.Apps.SetShortcutLaunchOptions({app}, "--show")')
    steam.run(f'SteamClient.Apps.SetShortcutIsVR({app}, true)')
    steam.run(f'SteamClient.Apps.SetShortcutIcon({app}, {json.dumps(str(ICON))})')
    for kind, name in ARTWORK.items():
        data = base64.b64encode((ART / name).read_bytes()).decode()
        steam.run(f'SteamClient.Apps.SetCustomArtworkForApp({app}, {json.dumps(data)}, "png", {kind})')
    print('Set the capsule, hero, logo, header and icon.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
