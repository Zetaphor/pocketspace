# Personal eye calibration

The engine accepts an optional JSON correction file. It is per wearer and headset
fit; changing the fit or reseating the headset can require recalibration. This
corrects the combined OpenVR gaze ray; it does not recalibrate SteamVR itself or
change foveation centers. No wearer correction is bundled in the repository.

Generate a correction with training and held-out validation targets: [eye-calibration.js](../scripts/eye-calibration.js).

## Generate in the headset

Run these commands from the checkout on the runtime host after building the engine. SteamVR must be running and the headset must have a
valid tracked pose. Close any primary scene application: the engine deliberately
suppresses gaze action polling while another scene process is active. Controller
finger sensing can still work while gaze is suppressed. See [tracking API](tracking-api.md)
for source validity, freshness, and coexistence behavior.

Fit the headset comfortably before starting and keep the calibration panel in
front of you. Choose a persistent output path whose parent directory exists,
then run:

```sh
mkdir -p "$HOME/.config/pocketspace"
./run-pocketspace.sh scripts/eye-calibration.js --eye-calibration-output "$HOME/.config/pocketspace/eye-calibration.json"
```

Look at each gold circle and tap either trigger. Keep your head and eyes steady
while the script waits 350 ms and samples for one second. It needs at least 30
valid, tracked samples. Captures whose 95th-percentile angular spread exceeds
1.5 degrees are rejected and can be retried. Bumper restarts the sequence.
There is no moving gaze dot to chase. Input is ignored for the first second of
script startup. Taps during an active capture do not start another capture. A
rejected capture stays on the same target; a successful one advances. Bumper
clears the current sequence but does not delete an already saved file. Script
reload also restarts the sequence. After success, stop with Ctrl-C and launch the
diagnostic below; saving does not stop the engine automatically.

Nine targets train an affine correction in head-relative yaw/pitch. Five different
targets validate it without entering the fit. Saving requires validation median
error at most 2 degrees, maximum at most 5 degrees, all validation points inside
the fitted domain, and no median degradation greater than 0.25 degrees from raw.
Each capture is represented by component-wise median raw and expected angles.
The spread gate uses the larger angular distance from those two medians for each
sample, so both gaze instability and head movement can reject a capture. The
validation metrics compare the five representative capture directions; they are
not per-frame error guarantees. The affine model is fitted by least squares to
the nine representative training pairs, with separate yaw and pitch coefficients.
These thresholds are practical gates, not hardware accuracy guarantees. Failed
validation leaves the output file untouched; retry the sequence or investigate
tracking quality. The runtime log reports `EYE_CALIBRATION_VALIDATION` metrics and
`EYE_CALIBRATION_SAVED` on success. Files are written through a sibling temporary
file and renamed; a successful run replaces an existing file at the chosen path.

## Load and consume

```sh
./run-pocketspace.sh scripts/tracking.js --eye-calibration "$HOME/.config/pocketspace/eye-calibration.json"
```

```js
const raw = getEyeTracking();
const corrected = getCalibratedEyeTracking();
if (corrected.valid && corrected.calibrationApplied) {
  // Use corrected.origin and corrected.direction for your interaction.
}
```

`scripts/tracking.js` displays the corrected green dot and correction status.
Check alignment across the panel, then turn your head slowly while looking at
the same targets.

`getEyeTracking()` remains raw. Both getters request the same native gaze polling.
The corrected getter adds `calibrationLoaded`, `calibrationApplied`, and
`calibrationStatus` (`notLoaded`, `gazeUnavailable`, `headUnavailable`, `applied`,
`outOfRange`). Unavailable corrections return the raw ray with those flags;
`valid` describes source gaze validity, not whether correction was applied.
Origin stays raw. Corrected target is reconstructed at the raw target's distance
along the corrected direction; it is not a new measurement of fixation depth.

Both getters expose `headBasis`: a row-major 3x3 matrix mapping head coordinates
(+X right, +Y up, +Z forward) into scene coordinates, or null without valid gaze
and a current head pose. The engine samples an unpredicted standing-space head
pose immediately before gaze polling. This is approximate synchronization;
OpenVR supplies no gaze capture timestamp. Calibration captures steady fixation; timing and changes in headset fit can
affect correction during motion.

`getEyeTrackingCalibration()` returns `{loaded, outputPath}`. The output path is
null unless `--eye-calibration-output FILE` was provided at startup.
`saveEyeTrackingCalibration(object)` validates the schema, atomically replaces
that path, activates the correction, and returns the path; failures throw.
Scripts cannot choose another write path. This low-level save API validates
format; the supplied generator performs the independent accuracy gate.
Loaded corrections persist across script reloads. Loading another file requires
restarting the engine. Invalid or missing input files fail startup explicitly.

## Startup options and lifecycle

| Option | Effect |
| --- | --- |
| `--eye-calibration FILE` | Load one correction before evaluating the scene script. Missing or invalid input fails startup with exit code 13. |
| `--eye-calibration-output FILE` | Allow `saveEyeTrackingCalibration()` to write this path. Does not load it, create parent directories, or check write access in advance. |

Options can appear before or after the script path. With no script path, the
engine uses `scripts/avocado.js`. The launcher forwards all options and changes
to the repository root; relative file paths therefore resolve from that root.
When invoking `build/pocketspace` directly, they resolve from your current
directory. There is no automatic default file, directory scan, or file watcher.
Keep personal files outside the repository or explicitly exclude them locally.

Both options may be supplied together, including the same path. This loads the
old correction at startup and permits replacing it after a new calibration.
The generator always fits raw `getEyeTracking()` data, so it does not stack a
correction on top of an existing one. A successful save activates the new
correction immediately for corrected getters in the same engine process. A failed
save leaves the active correction unchanged. Reset to raw-only behavior by
restarting without `--eye-calibration`; loading a new scene alone does not clear it.

Run the generator with the output option described above: [eye-calibration.js](../scripts/eye-calibration.js).

## Correction status and integration

All original source fields, including `status`, `tracked`, `fresh`, and
`suppressed`, retain their raw-gaze meaning. No correction creates valid gaze
from invalid input. The API does not automatically select, click, or grab objects.

| `calibrationStatus` | Meaning and returned geometry |
| --- | --- |
| `notLoaded` | No correction has been loaded or saved; returns raw geometry. |
| `gazeUnavailable` | A correction is loaded but source gaze is invalid; coordinates remain null. |
| `headUnavailable` | Source gaze is valid but the current head basis is unavailable; returns raw geometry. |
| `applied` | The corrected normalized direction and reconstructed target are returned. |
| `outOfRange` | Raw angles are outside the domain, behind the head, or corrected angles exceed the runtime safety bounds; returns raw geometry. |

Status is evaluated in the order above. For example, without a file an invalid
gaze has `calibrationStatus: "notLoaded"` and `valid: false`. Use both `valid` and
`calibrationApplied` when an interaction requires a corrected ray. If your scene
allows raw fallback, gate on `valid` and display or record the correction status.

Display correction status and use the corrected gaze ray: [tracking.js](../scripts/tracking.js).

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Startup asks for an output path | Start the generator with `--eye-calibration-output FILE`. |
| `sceneAppActive` / gaze suppressed | Close the primary scene application and wait for valid gaze. |
| Too few tracked samples | Wait for valid/tracked gaze, settle the headset, then retry the same target. |
| Unstable capture | Keep head still and look at the circle through the sampling interval; tap trigger again. |
| Degenerate fit | Bumper restarts all points. Confirm you actually look at each different target. |
| Validation failed | No file is saved. Bumper restarts training and validation; do not bypass the gate to claim accuracy. |
| Save failed | Check the runtime log, parent-directory existence, write access, and schema limits. After fixing the cause, bumper restarts the full sequence. |
| File loads but dot stays raw | Inspect `calibrationStatus` and ensure the scene calls `getCalibratedEyeTracking()`. |
| `outOfRange` near panel edges or after head turns | This sample is outside supported coverage; raw fallback is intentional. Recalibrate with an appropriate headset fit and panel position. |
| Previously good calibration becomes inaccurate | Refit/reseat the headset and repeat calibration. Choose the file for the current wearer. |

Inspect runtime availability and correction status in a headset HUD: [tracking.js](../scripts/tracking.js).

## File format

```json
{
  "version": 1,
  "source": "openvr",
  "model": "head-angular-affine",
  "units": "radians",
  "yaw": [0, 1, 0],
  "pitch": [0, 0, 1],
  "bounds": [-0.4, 0.4, -0.3, 0.3]
}
```

This is an identity example, not a usable personal calibration. For raw angles
`x = atan2(headX, headZ)` and `y = atan2(headY, hypot(headX, headZ))`, corrected yaw
is `yaw[0] + yaw[1]*x + yaw[2]*y`; pitch uses the same formula with `pitch`.
Bounds are raw `[minYaw,maxYaw,minPitch,maxPitch]`. The generator adds a 0.05-radian
margin around training coverage. Outside this domain, behind the head, or for
corrected angles beyond 1.4 radians, the getter falls back to raw. Coefficients
must be finite with absolute value at most 10; bounds must be ordered, finite,
and within ±1.5 radians. Extra metadata such as `createdAt` and `validation` is
allowed. Generated `createdAt` is an ISO UTC timestamp; generated `validation`
contains `targets` (5), `rawMedianDegrees`, `correctedMedianDegrees`, and
`maxDegrees`. Metadata is preserved in the saved JSON but is not used by the
runtime correction. Loading and the low-level save API enforce schema bounds,
not the generator's accuracy gate; a manually edited file need not have passed
wearer validation. There is no automatic identity/device matching: select the correct
wearer's file when launching.

Generate the calibration file instead of authoring coefficients by hand: [eye-calibration.js](../scripts/eye-calibration.js).
