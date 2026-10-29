#!/usr/bin/env python3
"""Exercise real HDR/EXR/PNG decoding and the custom-environment CLI pipeline."""
import subprocess
import sys
import tempfile
from pathlib import Path
import numpy as np
import OpenImageIO as oiio

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    for extension, value, space in [('hdr', 2., 'linear'), ('exr', 2., 'linear'),
                                    ('png', .5, 'srgb')]:
        source = folder / f'source.{extension}'
        output = oiio.ImageOutput.create(str(source))
        spec = oiio.ImageSpec(16, 8, 3, oiio.UINT8 if extension == 'png' else oiio.FLOAT)
        assert output.open(str(source), spec)
        assert output.write_image(np.full((8, 16, 3), value, dtype=np.float32))
        assert output.close()
        array = folder / f'{extension}.npy'
        subprocess.run([sys.executable, str(root/'tools/convert-environment.py'),
                        str(source), str(array), '--color-space', space], check=True)
        pixels = np.load(array, allow_pickle=False)
        expected = ((128/255+.055)/1.055)**2.4 if space == 'srgb' else value
        assert pixels.shape == (8, 16, 3)
        assert np.allclose(pixels, expected, atol=.005)
    bake = folder/'custom.sfenv'
    subprocess.run([sys.executable, str(root/'tools/generate-environment.py'),
                    str(bake), '--input', str(folder/'hdr.npy'), '--size', '16',
                    '--lut-size', '16', '--samples', '64'], check=True)
    assert bake.read_bytes()[:8] == b'SFENV001'
    rejected = subprocess.run([sys.executable, str(root/'tools/convert-environment.py'),
                              str(folder/'source.exr'), str(folder/'invalid.npy'),
                              '--color-space', 'srgb'], capture_output=True)
    assert rejected.returncode != 0
    assert not (folder/'invalid.npy').exists()
print('PASS: HDR/EXR radiance, sRGB decoding, bake pipeline and invalid encoding')
