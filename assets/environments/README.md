# Baked lighting environments

`studio.sfenv` is original procedural content under this repository's MIT license.
It contains a blue sky, warm ground, clear horizon and two bright windows. It is
lighting data only; the engine does not draw a skybox or capture the real room.

Rebuild it from the repository root with Python and NumPy:

```sh
python3 tools/generate-environment.py assets/environments/studio.sfenv
```

The default bake uses a 128-pixel cubemap, eight GGX-filtered roughness levels,
256 Hammersley samples, nine RGB diffuse spherical-harmonic coefficients and a
128×128 split-sum BRDF lookup. The file is 852,084 bytes. SHA-256:
`61a7e69b4d767e5d4c9a476ba15a756512429edc66fbb83dacdee50ddac24da9`.

To bake a custom environment, pass `--input image.npy`. The input must be an
H×W×3 linear HDR RGB equirectangular NumPy array, not sRGB pixels. Its top row is
+Y, center longitude is +X and the right quarter is +Z. Convert external HDR/EXR
images to that representation before baking; the runtime does not decode those
formats. Check the source image's licensing before redistributing a custom bake.

See [model lighting](../../docs/lighting-api.md) for loading and rotation controls.
