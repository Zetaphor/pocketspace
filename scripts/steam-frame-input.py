#!/usr/bin/env python3
"""Global raw Steam Frame input monitor via vrserver's local WebSocket.

Based on Frametop's input/vrws.py: https://github.com/Frametop/frametop/blob/main/input/vrws.py
"""
import argparse, base64, json, os, select, socket, struct, time, urllib.request

HOST, PORT = "127.0.0.1", 27062
REFERER = f"http://{HOST}:{PORT}/dashboard/controllerbinding.html"

def controllers():
    request = urllib.request.Request(f"http://{HOST}:{PORT}/input/getstate.json", headers={"Referer": REFERER})
    with urllib.request.urlopen(request, timeout=3) as response:
        state = json.load(response)
    return {device["root_path"]: device["side"] for device in state.get("devices", [])
            if device.get("controller_type") == "frame_controller"
            and device.get("side") in ("left", "right")}

def client_frame(opcode, payload):
    size = len(payload)
    header = bytes([0x80 | opcode])
    if size < 126:
        header += bytes([0x80 | size])
    elif size < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", size)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", size)
    mask = os.urandom(4)
    return header + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))

def main():
    parser = argparse.ArgumentParser(description="Monitor raw Steam Frame controller components globally")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this many seconds; 0 means forever")
    parser.add_argument("--changes-only", action="store_true", help="print only values differing from the previous update")
    args = parser.parse_args()
    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None

    sock = socket.create_connection((HOST, PORT), 3)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
               f"Origin: http://{HOST}:{PORT}\r\nReferer: {REFERER}\r\n\r\n")
    sock.sendall(request.encode())
    handshake = b""
    while b"\r\n\r\n" not in handshake:
        handshake += sock.recv(4096)
    sock.setblocking(False)

    mailbox = f"sf_overlay_input_{os.getpid()}"
    sock.sendall(client_frame(1, f"mailbox_open {mailbox}".encode()))
    sides = controllers()
    for path in sides:
        message = json.dumps({"type": "request_input_state_updates", "device_path": path,
                              "returnAddress": mailbox})
        sock.sendall(client_frame(1, ("mailbox_send input_server " + message).encode()))

    buffer, fragments, previous = b"", b"", {}
    while deadline is None or time.monotonic() < deadline:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buffer += chunk
        except BlockingIOError:
            select.select([sock], [], [], 0.25)
            continue
        while len(buffer) >= 2:
            first, second = buffer[0], buffer[1]
            size, header_size = second & 0x7F, 2
            if size == 126:
                if len(buffer) < 4:
                    break
                size, header_size = struct.unpack(">H", buffer[2:4])[0], 4
            elif size == 127:
                if len(buffer) < 10:
                    break
                size, header_size = struct.unpack(">Q", buffer[2:10])[0], 10
            if len(buffer) < header_size + size:
                break
            payload = buffer[header_size:header_size + size]
            buffer = buffer[header_size + size:]
            if first & 0x0F == 9:
                sock.sendall(client_frame(10, payload))
                continue
            if first & 0x0F not in (0, 1, 2):
                continue
            fragments += payload
            if not first & 0x80:
                continue
            try:
                message = json.loads(fragments.decode())
            except (UnicodeDecodeError, json.JSONDecodeError):
                fragments = b""
                continue
            fragments = b""
            side = sides.get(message.get("device"))
            if not side:
                continue
            for name, value in (message.get("components") or {}).items():
                key = (side, name)
                if args.changes_only and previous.get(key) == value:
                    continue
                previous[key] = value
                print(f"{side} {name} {json.dumps(value)}", flush=True)
    sock.close()

if __name__ == "__main__":
    main()
