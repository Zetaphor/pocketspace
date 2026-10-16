# Steam Frame Overlay MVP

Native aarch64 OpenVR overlay host + embedded QuickJS. Hello-overlay only:
one 256x256 GL texture + hot-reloadable JS module + Unix socket.

## Build (on deckard)
```
mkdir -p build && cd build && cmake .. && make -j2
```

## Run
```
./run-mvp.sh ~/.sf-mvp/script.js
```

Commands:
```
echo status | nc -U /tmp/sf-mvp.sock
echo reload | nc -U /tmp/sf-mvp.sock
echo quit   | nc -U /tmp/sf-mvp.sock
```

Hot-reload: edit ~/.sf-mvp/script.js; mtime poll picks it up (< poll 16ms), reload latency printed.
