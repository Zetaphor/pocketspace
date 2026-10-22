# Model selection regression

Run on Deckard from the repository root:

```sh
timeout -s TERM -k 5 180 ./run-overlay-engine.sh scripts/model-selection-test.js > /tmp/model-selection.log 2>&1
```

Use either bumper to advance. Release both triggers first. Cycling through all
six active cases resets their positions. Toy Car is temporarily excluded after
a wearer-reported Frametop disruption. Both custom lasers and trigger grabs are
enabled; depth and hand rotation retain their usual behavior.

| Case | Headset check |
| --- | --- |
| Hollow frame and rear box | Aim through the center: select the rear box. Aim at a blue rail: select the frame. The beam should stop at the selected surface. |
| Hollow frame alone | Aim through the center: no mesh hover or grab. Sweep the inner edges for false picks and hover flicker. |
| Rotated thin frame | Select narrow edges with either hand; the opening should remain empty. |
| Overlapping boxes | The nearer visible surface wins in the overlap; the exposed rear box remains selectable. |
| Lantern | Check handle, openings, silhouette, and hover stability. |
| Toy Car (excluded) | Profile separately before reintroducing; record FPS separately from selection accuracy. |
| Two distinct objects | Grab frame and Lantern simultaneously with different hands, move quickly, release independently, and check beam length lock. |

`SELECTION_CASE` records object IDs and labels. `SELECTION_EVENT` records mesh
hover and grab transitions with per-hand state. These logs describe engine
decisions; compare them with what the wearer aimed at and saw. Native surface
events alone are not evidence of a triangle hit.

The blue frame is a repository-owned synthetic fixture with a genuinely empty
center, 48 triangles, and a 0.16-unit depth before normalization. Regenerate it
with `python3 scripts/generate-selection-fixture.py`. It deliberately exposes
the original sphere fallback's ability to fill an opening. Do not mark this
regression passing without headset observations.

Record the engine revision, working-tree changes, runtime log, case, hand,
intended target, actual hover/grab, visible beam endpoint, and FPS. A low Toy Car
FPS does not establish that picking is the cause.

## October 22, 2026 smoke check

Deckard engine revision `d7db4b9`, with the new uncommitted test script and
fixture copied from Sigma. Build and fixture inspection succeeded. The bounded
25-second run logged `/tmp/model-selection-smoke-d7db4b9.log`, loaded all four
assets, created the two case-1 surfaces, reported 90.0–90.1 FPS and `gl_error=0`,
and exited with `completed frames=2117`; no engine remained afterward. Both
hands emitted mesh hover events, although left tracking was intermittent.
No wearer observations were collected, and cases 2–7 were not exercised.

## Wearer finding and pending fix verification

The wearer reported trouble in cases 1 and 2: the blue frame's empty center
was grabbable. Other cases were reported as not causing significant trouble;
this is not a detailed per-hand verification record.

The uncommitted fix on top of `d7db4b9` removes the spherical hit fallback;
bounds remain a rejection-only broad phase. Deckard rebuilt successfully.
A standalone geometry check compiled the actual picker functions and used the
fixture vertices to verify both hands: center miss, rail hit, rotated center
miss, and invalid tracking miss. Headset retest is pending in
`/tmp/model-selection-triangle-only-d7db4b9.log`.

The wearer then reported that the rear red box was unselectable in case 1 and
suspected Toy Car crashed Frametop. The triangle-only run logged roughly
34.9–35.3 FPS in the car case and later recovered to 90 FPS; the engine stopped
cleanly at 17029 frames. Frametop desktop was active when inspected; its logs
showed a session startup around the incident, but causation is unconfirmed.

Further inspection found uninitialized edge reads in `rayTriangle` and unset
normalized model bounds. Both are fixed in the current uncommitted source.
The optimized standalone check now includes an axis-aligned small triangle
hit/miss in addition to the hollow-frame checks. The earlier standalone test
supplied synthetic bounds and did not cover the model loader or rear box.
Deckard rebuilt successfully. The six-case retest excludes Toy Car entirely
(including loading) and logs to
`/tmp/model-selection-initialized-picker-d7db4b9.log`; wearer verification is pending.

That retest still grabbed the frame straight on; the rear box was selectable
only at an angle. The acquisition code allowed native rectangular surface
button events to override mesh selection. Current source disables that native
acquisition path for hands with a visible custom laser and requires a fresh
mesh hit for laser acquisition (the 100 ms hover grace cannot start a grab).
Existing held-object release and native panel input are unchanged.
The next bounded retest logs to `/tmp/model-selection-mesh-acquisition-d7db4b9.log`.

The wearer confirmed “That fixed it” after the straight-on rear-cube / empty-gap
retest instructions. This verifies the reported gap-selection regression in
cases 1–2; it does not establish a complete per-hand pass for all cases.
The final run reported 90 FPS and `gl_error=0`. Changes remain uncommitted on
top of `d7db4b9`; the exact deployed source matches the current Sigma files
`src/main.cpp` and `src/model_asset.cpp`.
