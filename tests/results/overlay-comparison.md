# Single versus grouped overlay experiment

Steam Frame native runs using common-plane renderer source `d491978`. The 3D scene
uses one visible stereo surface in single mode, or four body groups plus a floor
group in grouped mode. Both retain hidden staging handles and a separate HUD.

## Compact layout: requested density retained

All cases use 0.14-meter objects, 0.16-meter spacing, a 1.5-meter floor, and a
0.8-meter forward offset. Reference resolution is 768. No compact run reported
a resolution-limit warning. Each run lasts 12 seconds; medians discard the
first 5 seconds and retain 7 samples. Model pairs were repeated in reverse order.
Bodies are kinematic, interaction and lasers are disabled, and all cases load the
same asset cache. This isolates rendering; interactive physics performance differs.

| Geometry | Bodies | Single FPS | Grouped FPS | Single GPU estimate (MiB) | Grouped GPU estimate (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| boxes | 48 | 51.2 | 74.0 | 536.7 | 382.7 |
| boxes | 96 | 49.5 | 68.5 | 536.7 | 434.7 |
| models | 48 | 23.7–27.4 | 35.0 | 536.7 | 356.7 |
| models | 96 | 15.4–18.2 | 15.6–17.5 | 536.7 | 408.7 |

Grouping is faster for boxes and 48 detailed models. At 96 detailed models,
the relative FPS result reverses between passes and is inconclusive. Single
mode lowers CPU usage but allocates more GPU memory in this layout. These
short runs do not establish a universal rendering-mode winner.

## Wide layout: resolution ceiling reached

The initial profile uses 0.2-meter spacing, a 1.8-meter floor, and no forward
offset. Every single-mode run hits the 4096-pixel ceiling; grouped runs do not.
Single-model FPS can look better here, but that comparison is not quality-matched.
Do not treat it as evidence that a single surface preserves clarity at less cost.

The [raw medians and resolution metadata](overlay-comparison.json) preserve
both layouts and the reverse-order model pass. Full native logs remain in
`/tmp/sf-single-comparison*` on the runtime host. Headset visual acceptance is
pending; use the [experiment controls](../README.md) to compare motion, grabbing,
and close-up ToyCar/Avocado detail before selecting a default.
