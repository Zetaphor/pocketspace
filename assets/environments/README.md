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

For custom HDR/EXR or sRGB panoramas, use `tools/convert-environment.py` followed
by `tools/generate-environment.py`. See the [complete custom-environment workflow](../../docs/lighting-api.md#baking-a-custom-environment)
for dependencies, color spaces, quality settings, deployment and API usage.
Check the source image's licensing before redistributing a custom bake.
