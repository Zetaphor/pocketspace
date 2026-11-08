# Memory retention after scripts stop

Measured 2026-11-07 on Steam Frame with SteamVR running: a fresh engine, then
lighting-studio started and stopped three times and basketball twice, each
running 20 seconds and sampled 12 seconds after stopping. The sampler read the
engine's `smaps_rollup` and every `drm-*-memory` key per DRM client from fdinfo,
plus `pocketspace-cli stats`.

Earlier reports found about 190 MiB RSS and 685 MiB of driver GPU memory kept
after all demos stopped, against 62 MiB and 7 MiB fresh.

## Findings

Nothing grew from one cycle to the next, so the retained memory is not a leak.

- **Heap.** RSS after stops was 235–360 MiB and moved up and down between cycles,
  while live allocations returned to their earlier size. glibc keeps freed arena
  space, as [memory-reload.md](memory-reload.md) also found.
- **Engine GL client.** The Zink client kept about 420 MiB after each
  lighting-studio stop, the same in all three cycles. Mesa keeps freed buffers for
  reuse and releases them later: during the second basketball run it fell to
  161 MiB, and to 120 MiB after that stop.
- **SteamVR client device.** A second DRM client of about 258 MiB appears when the
  first scene starts and stays. Only about 1 MiB of it is resident; it belongs to
  the OpenVR client library, which imports the engine's overlay textures.

## Heap trim

The engine now calls `malloc_trim(0)` at the end of any frame that tore down a
script. RSS after stops:

| | Fresh | lighting-studio stops | basketball stops |
|---|---|---|---|
| Before | 63 MiB | 237 / 360 / 235 MiB | 237 / 237 MiB |
| With trim | 59 MiB | 74 / 68 / 68 MiB | 71 / 71 MiB |

The trim took up to about 40 ms after a lighting-studio stop and under 1.2 ms
otherwise. Running it on a worker thread did not shorten the frame stall: the
longest frame after each lighting-studio stop (24.8–43.0 ms) matched the worker's
trim time (14.9–41.4 ms), because allocations on the frame loop wait for the arena
lock the trim holds. The trim therefore runs on the frame loop. Driver GPU memory
is left to Mesa.
