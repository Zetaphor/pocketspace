# Personal eye calibration

The engine accepts an optional JSON correction file. It is per wearer and headset
fit; changing the fit or reseating the headset can require recalibration. This
corrects the combined OpenVR gaze ray; it does not recalibrate SteamVR itself or
change foveation centers. No wearer correction is bundled in the repository.

## Generate in the headset

Choose an output path whose parent directory exists, then run on Deckard:

```sh
./run-overlay-engine.sh scripts/eye-calibration.js --eye-calibration-output /home/steamos/eye-calibration.json
```

Look at each gold circle and tap either trigger. Keep your head and eyes steady
while the script waits 350 ms and samples for one second. It needs at least 30
valid, tracked samples. Captures whose 95th-percentile angular spread exceeds
1.5 degrees are rejected and can be retried. Bumper restarts the sequence.
There is no moving gaze dot to chase.

Nine targets train an affine correction in head-relative yaw/pitch. Five different
targets validate it without entering the fit. Saving requires validation median
error at most 2 degrees, maximum at most 5 degrees, all validation points inside
the fitted domain, and no median degradation greater than 0.25 degrees from raw.
These thresholds are practical gates, not hardware accuracy guarantees. Failed
validation leaves the output file untouched; retry the sequence or investigate
tracking quality. The runtime log reports `EYE_CALIBRATION_VALIDATION` metrics and
`EYE_CALIBRATION_SAVED` on success. Files are written through a sibling temporary
file and renamed; a successful run replaces an existing file at the chosen path.

## Load and consume

```sh
./run-overlay-engine.sh scripts/tracking-test.js --eye-calibration /home/steamos/eye-calibration.json
```

```js
const raw = getEyeTracking();
const corrected = getCalibratedEyeTracking();
if (corrected.valid && corrected.calibrationApplied) {
  // Use corrected.origin and corrected.direction for your interaction.
}
```

`scripts/tracking-test.js` displays the corrected green dot and correction status.
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
OpenVR supplies no gaze capture timestamp. Calibration is designed for steady
fixation and has not yet been validated during rapid head motion.

`getEyeTrackingCalibration()` returns `{loaded, outputPath}`. The output path is
null unless `--eye-calibration-output FILE` was provided at startup.
`saveEyeTrackingCalibration(object)` validates the schema, atomically replaces
that path, activates the correction, and returns the path; failures throw.
Scripts cannot choose another write path. This low-level save API validates
format; the supplied generator performs the independent accuracy gate.
Loaded corrections persist across script reloads. Loading another file requires
restarting the engine. Invalid or missing input files fail startup explicitly.

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
allowed. There is no automatic identity/device matching: select the correct
wearer's file when launching.

## Verification

```sh
node tests/eye-calibration-test.cjs
c++ -std=c++20 -O2 -Wall -Wextra -I src tests/eye-calibration-math.cpp -o /tmp/eye-calibration-math
/tmp/eye-calibration-math
```

Synthetic tests cover known affine recovery, held-out validation, unstable/invalid
capture rejection, failed validation refusing save, head rotation, domain limits,
and an unloaded correction. `tests/eye-calibration-smoke.js` tests native file
round trips and identity correction; use only a disposable output file because
it deliberately writes an identity fixture. Wearer calibration and subsequent
head-turn verification remain required before claiming real accuracy improvement.

Initial Deckard Release build, native save/load round trip, identity-ray check,
and malformed-file startup rejection passed. The last smoke expansion and
corrected diagnostic update were made after Deckard stopped resolving; final
Deckard synchronization/build and generator startup remain pending connectivity.
