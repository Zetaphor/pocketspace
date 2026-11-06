# Demo props

These models are original procedural content under this repository's
AGPL-3.0-only license. They are untextured, with solid PBR colours per part.
Rebuild them from the repository root with Python's standard library:

```sh
python3 tools/generate-demo-models.py
```

The engine centers each model on its bounds and normalizes its largest
dimension to one, so pass the **scale** below to `createObject` for real size
and place the object at its **center**. Sizes and centers are in meters. Models
that belong together share an authored frame, so their centers keep them
aligned.

| Model | Scale | Size (x × y × z) | Center | Parts |
| --- | --- | --- | --- | --- |
| `Basketball.glb` | 0.1806 | 0.1806 × 0.1806 × 0.1806 | 0, 0, 0 | Orange ball, dark raised seams |
| `HoopPost.glb` | 1.76 | 0.4 × 1.76 × 0.45 | 0, 0.88, 0.025 | Base plate, pole, arm to the board |
| `HoopBackboard.glb` | 0.6 | 0.6 × 0.4 × 0.022 | 0, 1.76, 0.261 | White board, border and target square on the +Z face |
| `HoopRim.glb` | 0.378 | 0.328 × 0.014 × 0.378 | 0, 1.6, 0.461 | Ring and bracket |
| `Desk.glb` | 0.9 | 0.9 × 0.03 × 0.5 | 0, 0, 0 | Wooden top with rounded corners and edges |
| `CradleFrame.glb` | 0.32 | 0.32 × 0.224 × 0.18 | 0, 0.112, 0 | Black base, chrome uprights and rails |
| `CradleBall.glb` | 0.1417 | 0.04 × 0.1405 × 0.1417 | 0, −0.0698, 0 | Steel ball and two strings |

## Hoop

The hoop's frame has the floor at y 0 under the pole and +Z toward the player.
The board's front face is at z 0.272. The rim's inside diameter is 0.30 m, its
tube is 0.014 m thick, and its ring is centered at (0, 1.6, 0.486), level with
the bottom of the target square. The basketball is 0.18 m across. For a smaller
or larger hoop, multiply every part's scale and center, and the ball's scale,
by the same factor.

## Newton's cradle

The frame's origin is the bottom center of the base. The rails run along x at
y 0.22, z ±0.07, from x −0.14 to 0.14. A cradle ball's origin is its pivot,
midway between the tops of its two strings; in the frame it sits at
(x, 0.22, 0), between the rails. The ball is 0.04 m across with its
center 0.12 m below the pivot, so it swings on a hinge whose axis runs along z
through the pivot. Five touching balls sit at x −0.08, −0.04, 0, 0.04 and 0.08.
