# Demo resource usage

Measured 2026-11-06 on Steam Frame (5 online cores, 15.3 GiB unified memory,
Adreno GPU at up to 903 MHz) with the headset active and SteamVR running.
Firefox, Vesktop and Claude Code were also running in the background, so
system-wide CPU figures include their load.

## Method

Each capture averages 30 one-second samples, taken after the scene had settled
for 12–15 seconds. The sampler reads:

- system CPU from `/proc/stat` and RAM as `MemTotal - MemAvailable`;
- per-process CPU (100% = one core) from `/proc/<pid>/stat`, and RSS/PSS from
  `smaps_rollup`;
- per-process GPU busy time and memory from DRM fdinfo (`drm-engine-gpu`,
  `drm-total-memory`, `drm-shared-memory`), deduplicated by `drm-client-id`;
  total GPU busy is the sum over all clients;
- GPU clock from `/sys/class/devfreq/3d00000.gpu/cur_freq`.

Engine FPS, per-script GPU estimates and callback time come from
`pocketspace-cli stats`. The engine was started with
`pocketspace-cli start-engine` (user service, no autostart scripts) and the
demos with `pocketspace-cli start demos/<name>.js` from the installed catalog.

## All demos together

| | Baseline (SteamVR only) | Engine, no scripts | All 8 demos |
|---|---|---|---|
| Engine FPS | — | 90 | **28** |
| Engine CPU (one core = 100%) | — | 3.7% | 42.7% (40.4–45.7%) |
| Engine RSS | — | 62 MiB | 408 MiB |
| Engine GPU memory (driver) | — | 7 MiB | 1,263 MiB (749 MiB private) |
| Engine GPU busy | — | ~0% | 38.6% (32.6–41.8%) |
| Compositor CPU | 14.8% | 14.7% | 20.0% |
| Compositor GPU memory | 840 MiB | 840 MiB | 1,420 MiB |
| Compositor GPU busy | 24.9% | 25.4% | 38.9% |
| System RAM used | 9,936 MiB | 9,959 MiB (+23) | 11,723 MiB (+1,787) |
| Total GPU busy | 26% at 581 MHz | 27% at 567 MHz | 78% at 903 MHz |
| System CPU (all cores) | 37% | 35% | 38% |

Per-script figures from `pocketspace-cli stats` with all demos running:

| Demo | JS heap | Objects | Panels | Assets | Bodies | GPU (est.) | Callback ms/s | Worst ms |
|---|---|---|---|---|---|---|---|---|
| clock | 227.6 KiB | 1 | 1 | 1 | 0 | 2.2 MiB | 4.17 | 1.37 |
| monitor | 223.7 KiB | 1 | 1 | 1 | 0 | 2.1 MiB | 8.44 | 3.84 |
| sketch | 228.2 KiB | 0 | 1 | 0 | 0 | 2.8 MiB | 5.76 | 1.53 |
| notes | 213.4 KiB | 2 | 2 | 3 | 0 | 4.6 MiB | 4.33 | 2.15 |
| basketball | 222.4 KiB | 9 | 1 | 7 | 8 | 229.2 MiB | 7.01 | 1.39 |
| desk-toys | 228.4 KiB | 9 | 1 | 5 | 9 | 46.2 MiB | 9.01 | 1.37 |
| pet-fox | 216.3 KiB | 1 | 0 | 1 | 0 | 12.0 MiB | 6.17 | 0.93 |
| lighting-studio | 220.0 KiB | 7 | 1 | 7 | 0 | 350.2 MiB | 4.41 | 1.11 |

Engine total GPU allocation estimate: 650.6 MiB, of which 1.1 MiB is shared
between scripts.

### Findings

- The idle engine is cheap: about 4% of one core, 62 MiB RSS and 7 MiB of GPU
  memory at 90 FPS.
- All demos together add about 1.75 GiB of system memory: about 350 MiB of
  engine RSS, about 740 MiB of engine GPU allocations and about 580 MiB more in
  the compositor (likely the engine's imported overlay and stereo textures).
- The frame-rate drop is GPU-bound: the GPU reaches its maximum clock and total
  busy time triples. The engine uses under half of one core.
- Script callbacks total about 50 ms per second across all demos, with no
  callback over 3.8 ms.
- The engine reports driver GPU memory as unavailable because
  `src/runtime_stats.h` reads `drm-memory-total:`, while the msm driver writes
  `drm-total-memory:`. The monitor demo therefore shows `n/a` for GPU memory.

## Each demo alone

Each demo ran by itself on one engine, one after another, in the order shown:
15 seconds to settle, then 30 seconds sampled while `pocketspace-cli --json
stats` was polled every 2 seconds for FPS and callback time. Each demo was then
stopped, with 8 seconds before the next. The idle row is the same engine before
the first demo, after a short clock run used to check the procedure.

| Demo | FPS (min–max) | Engine CPU | Engine RSS | Engine GPU mem (private) | GPU est. | Engine GPU busy | Compositor GPU busy | Total GPU busy @ MHz | System RAM Δ | Compositor GPU mem Δ | Callback ms/s (worst) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| idle | 90.0 (90–90) | 4.2% | 70 MiB | 281 (275) MiB | 0.0 MiB | 0.0% | 25.9% | 27% @ 776 | +0 MiB | +0 MiB | 0.00 (0.00) |
| clock | 90.0 (90–90) | 8.8% | 72 MiB | 277 (270) MiB | 2.2 MiB | 0.8% | 25.8% | 33% @ 838 | +186 MiB | +7 MiB | 9.53 (5.56) |
| monitor | 90.0 (89–90) | 10.2% | 73 MiB | 279 (272) MiB | 2.1 MiB | 1.3% | 25.5% | 28% @ 632 | +220 MiB | +4 MiB | 14.99 (13.91) |
| sketch | 89.8 (88–91) | 10.5% | 75 MiB | 292 (281) MiB | 2.8 MiB | 2.2% | 24.7% | 28% @ 641 | +30 MiB | +5 MiB | 17.14 (26.79) |
| notes | 89.9 (90–90) | 12.0% | 73 MiB | 381 (297) MiB | 37.1 MiB | 5.2% | 22.3% | 28% @ 674 | +177 MiB | +71 MiB | 14.31 (8.29) |
| basketball | 88.7 (88–90) | 17.8% | 79 MiB | 799 (376) MiB | 275.0 MiB | 38.8% | 33.9% | 74% @ 903 | +533 MiB | +394 MiB | 17.00 (6.16) |
| desk-toys | 90.0 (90–90) | 29.3% | 80 MiB | 386 (311) MiB | 39.0 MiB | 7.8% | 21.9% | 30% @ 839 | +104 MiB | +87 MiB | 13.46 (2.01) |
| pet-fox | 90.0 (90–90) | 11.4% | 88 MiB | 330 (301) MiB | 12.0 MiB | 2.5% | 24.5% | 28% @ 606 | -5 MiB | +16 MiB | 10.95 (1.94) |
| lighting-studio | 82.5 (77–89) | 53.2% | 385 MiB | 730 (659) MiB | 346.1 MiB | 44.8% | 21.1% | 67% @ 903 | +730 MiB | +65 MiB | 13.05 (5.08) |

System RAM deltas are relative to the idle row and include background
applications, so changes under about 250 MiB are within noise; the engine and
compositor columns are more reliable. Driver GPU memory includes what the engine
kept after earlier demos (see below), so the engine's GPU estimate is the better
per-demo figure.

### Findings

- Only lighting-studio misses 90 FPS on its own (82.5 average, 77 lowest). It
  also uses the most engine CPU (53% of one core, against about 4% idle) and
  the most memory (346 MiB GPU estimate, about 315 MiB more engine RSS).
- Basketball is the heaviest GPU load relative to its content: total GPU busy
  reaches 74% at the maximum clock, with a 275 MiB GPU estimate and 394 MiB
  more compositor GPU memory. It holds about 89 FPS alone, with little headroom.
- Lighting studio and basketball each push the GPU to its maximum clock, so
  running both exceeds it. That accounts for the 28 FPS with every demo
  running. The other six demos together add little GPU load.
- Desk-toys uses about 25% more of one core than idle, presumably for physics
  stepping, but it holds 90 FPS.
- Worst single callbacks: sketch reached 26.8 ms and monitor 13.9 ms, both
  above the 11.1 ms frame at 90 Hz, so each can cause an occasional dropped
  frame. Nobody interacted with the demos during the run.
- The engine does not return to its fresh idle footprint after its scripts stop.
  A fresh engine used 62 MiB RSS and 7 MiB of driver GPU memory. After running
  the demos and stopping them all, it used 192 MiB RSS and about 685 MiB of
  driver GPU memory across two DRM clients (418 MiB and 267 MiB), while its own
  GPU estimate was 0 B. The 267 MiB client was already present after the short
  initial clock run. It is not yet known whether this is a driver buffer cache,
  allocator retention or a leak.

## All demos together after the surface fixes

Measured 2026-11-07 with the same method, after the render-group density cap and
independent-surface allocation changes in
[display-density-cap.md](display-density-cap.md). Background applications used
more memory than on 2026-11-06 (11.8 GiB against 9.9 GiB with SteamVR only), so
compare system RAM as increases.

| | Baseline (SteamVR only) | Engine, no scripts | All 8 demos | All 8 demos, 2026-11-06 |
|---|---|---|---|---|
| Engine FPS | — | 90 | **28.2** (min 24) | 28 |
| Engine CPU (one core = 100%) | — | 3.8% | 43.5% | 42.7% |
| Engine RSS | — | 62 MiB | 406 MiB | 408 MiB |
| Engine GPU memory (driver) | — | 7 MiB | 1,103 MiB (723 MiB private) | 1,263 MiB (749 MiB private) |
| Engine GPU estimate | — | 0 | 559 MiB | 651 MiB |
| Engine GPU busy | — | 0% | 31.3% | 38.6% |
| Compositor CPU | 13.4% | 13.3% | 20.6% | 20.0% |
| Compositor GPU memory | 776 MiB | 776 MiB | 1,193 MiB | 1,420 MiB |
| Compositor GPU busy | 23.0% | 23.8% | 40.8% | 38.9% |
| System RAM used | 11,810 MiB | +65 MiB | +1,646 MiB | +1,787 MiB |
| Total GPU busy | 30% at 879 MHz | 30% at 903 MHz | 77% at 903 MHz | 78% at 903 MHz |

| Demo | JS heap | Objects | Panels | Assets | Bodies | GPU (est.) | Callback ms/s | Worst ms |
|---|---|---|---|---|---|---|---|---|
| clock | 227.6 KiB | 1 | 1 | 1 | 0 | 3.1 MiB | 5.15 | 0.89 |
| monitor | 223.7 KiB | 1 | 1 | 1 | 0 | 3.0 MiB | 6.46 | 2.80 |
| sketch | 304.2 KiB | 0 | 1 | 0 | 0 | 2.8 MiB | 3.00 | 1.21 |
| notes | 229.8 KiB | 5 | 5 | 3 | 0 | 27.3 MiB | 4.40 | 1.08 |
| basketball | 222.4 KiB | 9 | 1 | 7 | 8 | 67.4 MiB | 8.20 | 1.67 |
| desk-toys | 228.4 KiB | 9 | 1 | 5 | 9 | 76.0 MiB | 6.86 | 1.25 |
| pet-fox | 216.3 KiB | 1 | 0 | 1 | 0 | 14.2 MiB | 5.02 | 0.98 |
| lighting-studio | 220.0 KiB | 7 | 1 | 7 | 0 | 364.6 MiB | 5.98 | 1.07 |

### Findings

- GPU memory fell: 160 MiB less driver memory in the engine, 227 MiB less in the
  compositor and a 92 MiB smaller estimate. Basketball's estimate fell from
  229 MiB to 67 MiB. Notes rose because it held five notes instead of two.
  Desk-toys rose from 46 MiB to 76 MiB, probably from the 25% allocation headroom
  its independent surfaces now keep; this was not isolated.
- Engine GPU busy fell from 38.6% to 31.3%, but the frame rate did not change.
- The engine's frame profile showed 29.5 ms of GPU completion wait and 2.7 ms of
  submission per frame. The engine called `glFinish` once per running scene, so
  nine scenes meant nine serialized GPU round-trips per frame, each queued behind
  the compositor's work. The engine's GPU work itself was not the limit.
- No callback exceeded 2.8 ms.

### One GPU wait per frame

The engine now renders every scene, calls `glFinish` once, then submits each
scene's surfaces. Measured the same way on 2026-11-07, with the user confirming
no new flicker, tearing or half-drawn objects:

| | Per-scene wait | One wait per frame |
|---|---|---|
| Engine FPS | 28.2 (min 24) | **45.0** (min 41) |
| GPU completion wait per frame | 29.5 ms | 5–16 ms |
| Engine CPU (one core = 100%) | 43.5% | 55.7% |
| Engine GPU busy | 31.3% | 49.5% |
| Compositor GPU busy | 40.8% | 41.0% |
| Total GPU busy | 77% at 903 MHz | 96% at 903 MHz |
| Engine RSS | 406 MiB | 411 MiB |
| Engine GPU memory (driver) | 1,103 MiB (723 private) | 1,133 MiB (753 private) |
| System RAM increase over idle engine | +1,581 MiB | +1,421 MiB |

The GPU now runs at its maximum clock and is nearly fully busy, so further gains
need less GPU work, chiefly lighting-studio's point-light shadows. The worst
callback was 4.2 ms (monitor).
