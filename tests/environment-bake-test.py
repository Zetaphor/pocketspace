#!/usr/bin/env python3
"""Independent constant-radiance, cube orientation and energy checks for the baker."""
import importlib.util
from pathlib import Path
import numpy as np

spec = importlib.util.spec_from_file_location('environment_bake', Path(__file__).resolve().parents[1]/'tools/generate-environment.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)
color = np.array([.25, .5, 1.])
constant = lambda d: np.broadcast_to(color, d.shape)
axes = np.array([[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]])
for face, axis in enumerate(axes):
    assert np.allclose(bake.face_directions(face, 1)[0], axis)
for roughness in [0, .25, .5, 1]:
    assert np.allclose(bake.prefilter(constant, axes, roughness, 128), color, atol=1e-8)
sh = bake.diffuse_sh(constant)
assert np.allclose(bake.sh_basis(axes) @ sh, color, atol=1e-4)
lut = bake.brdf_lut(32, 512)
assert np.isfinite(lut).all() and lut.min() >= 0 and np.max(lut.sum(axis=-1)) < 1.02
assert np.allclose(bake.image_source(np.ones((8, 16, 3)))(axes), 1)
# The rough GGX environment must broaden a small bright source instead of
# treating an ordinary box-filtered mip chain as a roughness convolution.
sharp = bake.studio(bake.face_directions(2, 32))
rough = bake.prefilter(bake.studio, bake.face_directions(2, 32), 1, 512)
assert sharp.max()-sharp.min() > rough.max()-rough.min()
print('PASS environment bake: constant radiance, diffuse SH normalization, six cube axes, bounded BRDF energy, equirectangular sampling and GGX roughness filtering')
