#!/usr/bin/env python3
"""Convert an RGB equirectangular panorama to linear NumPy input for the baker."""
import argparse
from pathlib import Path
import numpy as np


def linear_rgb(pixels, color_space):
    pixels = np.asarray(pixels, dtype=np.float32)
    if pixels.ndim != 3 or pixels.shape[2] < 3 or min(pixels.shape[:2]) < 2:
        raise ValueError('expected an RGB equirectangular image at least 2×2')
    pixels = pixels[..., :3]  # Panorama alpha is not lighting data.
    if not np.isfinite(pixels).all() or pixels.min() < 0 or pixels.max() > 65504:
        raise ValueError('RGB values must be finite, nonnegative and at most 65504')
    if color_space == 'srgb':
        if pixels.max() > 1:
            raise ValueError('sRGB input must be in [0, 1]; use linear for HDR radiance')
        pixels = np.where(pixels <= .04045, pixels / 12.92,
                          ((pixels + .055) / 1.055) ** 2.4)
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, help='output .npy file')
    parser.add_argument('--color-space', choices=['linear', 'srgb'], required=True,
                        help='source encoding; linear Rec.709/sRGB primaries for HDR/EXR')
    args = parser.parse_args()
    if args.output.suffix.lower() != '.npy':
        parser.error('output must end in .npy')
    try:
        import OpenImageIO as oiio
    except ImportError:
        parser.error('install tools/requirements-environment.txt in your Python environment')
    image = oiio.ImageInput.open(str(args.input))
    if not image:
        parser.error(f'cannot open image: {oiio.geterror()}')
    try:
        pixels = image.read_image(format=oiio.FLOAT)
        if pixels is None:
            raise ValueError(f'cannot read image: {image.geterror()}')
        pixels = linear_rgb(pixels, args.color_space)
    except ValueError as error:
        parser.error(str(error))
    finally:
        image.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.save(args.output, pixels, allow_pickle=False)
    print(f'Wrote {args.output}: {pixels.shape[1]}×{pixels.shape[0]} linear RGB')


if __name__ == '__main__':
    main()
