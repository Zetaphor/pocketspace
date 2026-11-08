# Display density cap

Measured 2026-11-07 on Steam Frame with the headset worn, facing the basketball
hoop from the starting position, and SteamVR running. Each row averages 30
one-second samples after 15 seconds of settling, using the method in
[demo-resource-usage.md](demo-resource-usage.md). Background applications were
running, so compare rows with each other rather than with that report.

## Cause

Basketball's four hoop parts share render group 1. Group density is the most
demanding member's reference density in pixels per meter: `eyePixels` over the
member's reference width, at least 0.55 m. The small rim and net give
768 / 0.55 ≈ 1,400 pixels per meter regardless of distance. Spread over the
roughly 2.2 m wide hoop at 2 m, the group requested 3,000–4,096 pixels per eye
and logged `group=1 reaches 4096 pixels per eye`. The headset resolves about a
third of that density at 2 m.

The engine now caps every surface at 1.25 times the headset's pixels per meter at
the surface's depth, using the projection that LOD already queries. Reference
density still applies when the surface is closer.

## Basketball

| Variant | FPS (min) | Engine CPU | Engine GPU busy | Compositor GPU busy | Total GPU @ MHz | GPU estimate | Engine GPU mem | Compositor GPU mem |
|---|---|---|---|---|---|---|---|---|
| Shipped demo, uncapped engine | 86.5 (83) | 18.8% | 40.9% | 38.8% | 87% @ 903 | 285.0 MiB | 785 MiB | 1,158 MiB |
| Same code in scratch | 87.6 (83) | 17.7% | 42.3% | 37.1% | 87% @ 903 | 269.4 MiB | 775 MiB | 1,155 MiB |
| Hoop at `eyePixels` 384 | 90.0 (90) | 17.7% | 15.4% | 29.6% | 52% @ 903 | 99.4 MiB | 476 MiB | 929 MiB |
| Hoop as independent surfaces | 89.9 (89) | 22.7% | 10.2% | 26.9% | 45% @ 903 | 50.2 MiB | 399 MiB | 875 MiB |
| Shipped demo, capped engine | 90.0 (90) | 19.1% | 5.5% | 26.5% | 40% @ 903 | 35.4 MiB | 369 MiB | 808 MiB |

The two demo-side variants were run from the scratch scene with the demo's code
and one change each; they confirm that surface size, not geometry, set the cost.
The capped engine needs no demo change. The user confirmed in the headset that
the hoop and balls look as sharp as before from the start position, up close at
the rim and held near the face, with no visible pops while walking toward the
hoop.
