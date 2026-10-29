#!/usr/bin/env python3
"""Bake a linear-HDR studio or equirectangular NumPy image into SFENV001.

Requires NumPy. All integrations happen offline; the runtime only uploads textures.
The studio is original procedural content under the repository's MIT license.
"""
import argparse
from pathlib import Path
import struct
import numpy as np


def unit(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def studio(d):
    y = d[..., 1:2]
    sky = np.array([.65, .85, 1.2]) * (.6 + .4 * np.maximum(y, 0))
    ground = np.broadcast_to(np.array([.12, .10, .08]), d.shape)
    result = np.where(y >= 0, sky, ground).copy()
    # Broad windows add recognizable reflections without a visible scene skybox.
    for direction, color in [([-.6, .65, .45], [4., 3.8, 3.5]),
                             ([.8, .4, -.35], [2., 2.4, 3.])]:
        cos = np.sum(d * unit(np.array(direction)), axis=-1, keepdims=True)
        result += np.clip((cos - .94) / .025, 0, 1) * np.array(color)
    return result


def image_source(image):
    def sample(d):
        u = (np.arctan2(d[..., 2], d[..., 0]) / (2*np.pi) + .5) * image.shape[1] - .5
        v = np.arccos(np.clip(d[..., 1], -1, 1)) / np.pi * image.shape[0] - .5
        x = np.floor(u).astype(int)
        y = np.floor(v).astype(int)
        tx = (u-x)[..., None]
        ty = (v-y)[..., None]
        a = image[np.clip(y, 0, image.shape[0]-1), x % image.shape[1]]
        b = image[np.clip(y, 0, image.shape[0]-1), (x+1) % image.shape[1]]
        c = image[np.clip(y+1, 0, image.shape[0]-1), x % image.shape[1]]
        e = image[np.clip(y+1, 0, image.shape[0]-1), (x+1) % image.shape[1]]
        return (a*(1-tx)+b*tx)*(1-ty)+(c*(1-tx)+e*tx)*ty
    return sample


def sequence(count):
    i = np.arange(count, dtype=np.uint32)
    bits = i.copy()
    bits = (bits << 16) | (bits >> 16)
    for mask, shift in [(0x55555555, 1), (0x33333333, 2), (0x0F0F0F0F, 4), (0x00FF00FF, 8)]:
        bits = ((bits & mask) << shift) | ((bits >> shift) & mask)
    return np.stack([i/count, bits.astype(float)/2**32], axis=-1)


def half_vectors(roughness, samples):
    xi = sequence(samples)
    a2 = roughness**4
    cos = np.sqrt((1-xi[:, 1]) / (1+(a2-1)*xi[:, 1]))
    sin = np.sqrt(np.maximum(1-cos*cos, 0))
    phi = 2*np.pi*xi[:, 0]
    return np.stack([sin*np.cos(phi), sin*np.sin(phi), cos], axis=-1)


def face_directions(face, size):
    a = (np.arange(size)+.5)/size*2-1
    x, y = np.meshgrid(a, a)
    one = np.ones_like(x)
    # OpenGL cube face order and lower-left texture row convention.
    directions = [(one, -y, -x), (-one, -y, x), (x, one, y),
                  (x, -one, -y), (x, -y, one), (-x, -y, -one)]
    return unit(np.stack(directions[face], axis=-1)).reshape(-1, 3)


def prefilter(source, normals, roughness, samples):
    normals = np.asarray(normals, dtype=float)
    if roughness == 0:
        return source(normals)
    result = np.empty_like(normals)
    h = half_vectors(roughness, samples)
    for start in range(0, len(normals), 128):
        n = normals[start:start+128]
        hint = np.where(np.abs(n[:, 2:3]) < .999, [0, 0, 1], [1, 0, 0])
        tangent = unit(np.cross(hint, n))
        bitangent = np.cross(n, tangent)
        world_h = tangent[:, None]*h[None, :, 0:1] + bitangent[:, None]*h[None, :, 1:2] + n[:, None]*h[None, :, 2:3]
        l = unit(2*np.sum(n[:, None]*world_h, axis=-1, keepdims=True)*world_h-n[:, None])
        weight = np.maximum(np.sum(n[:, None]*l, axis=-1, keepdims=True), 0)
        result[start:start+len(n)] = np.sum(source(l)*weight, axis=1)/np.maximum(np.sum(weight, axis=1), 1e-12)
    return result


def sh_basis(d):
    x, y, z = np.asarray(d, dtype=float).T
    return np.stack([np.full_like(x, .2820947918), .4886025119*y, .4886025119*z,
                     .4886025119*x, 1.0925484306*x*y, 1.0925484306*y*z,
                     .3153915653*(3*z*z-1), 1.0925484306*x*z,
                     .5462742153*(x*x-y*y)], axis=-1)


def diffuse_sh(source):
    i = np.arange(32768)
    y = 1-2*(i+.5)/len(i)
    phi = i*np.pi*(3-np.sqrt(5))
    r = np.sqrt(1-y*y)
    d = np.stack([r*np.cos(phi), y, r*np.sin(phi)], axis=-1)
    radiance = sh_basis(d).T @ source(d) * (4*np.pi/len(d))
    return radiance * np.array([1, 2/3, 2/3, 2/3, .25, .25, .25, .25, .25])[:, None]


def brdf_lut(size, samples):
    nv = (np.arange(size)+.5)/size
    v = np.stack([np.sqrt(1-nv*nv), np.zeros(size), nv], axis=-1)
    result = np.zeros((size, size, 2))
    for row in range(size):
        roughness = max((row+.5)/size, .045)
        h = half_vectors(roughness, samples)
        vh = np.maximum(v @ h.T, 0)
        nl = np.maximum(2*vh*h[None, :, 2]-nv[:, None], 0)
        a2 = roughness**4
        visibility = .5/np.maximum(nl*np.sqrt(nv[:, None]**2*(1-a2)+a2) + nv[:, None]*np.sqrt(nl*nl*(1-a2)+a2), 1e-12)
        weight = 4*visibility*nl*vh/np.maximum(h[None, :, 2], 1e-12)
        fc = (1-vh)**5
        result[row, :, 0] = np.mean(weight*(1-fc), axis=1)
        result[row, :, 1] = np.mean(weight*fc, axis=1)
    return result


def bake(source, size=128, lut_size=128, samples=256):
    levels = size.bit_length()
    sh = diffuse_sh(source)
    payload = bytearray(b'SFENV001'+struct.pack('<III', size, levels, lut_size))
    payload.extend(np.asarray(sh, dtype='<f4').tobytes())
    for level in range(levels):
        width = max(1, size >> level)
        for face in range(6):
            values = prefilter(source, face_directions(face, width), level/(levels-1), samples)
            if not np.isfinite(values).all() or np.min(values) < 0 or np.max(values) > 65504:
                raise ValueError('environment radiance must be finite, nonnegative and fit float16')
            payload.extend(np.asarray(values, dtype='<f2').tobytes())
    payload.extend(np.asarray(brdf_lut(lut_size, samples), dtype='<f2').tobytes())
    return payload


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--input', type=Path, help='linear HDR RGB equirectangular .npy array; top row is +Y, center is +X, right quarter is +Z')
    parser.add_argument('--size', type=int, choices=[16, 32, 64, 128, 256], default=128)
    parser.add_argument('--lut-size', type=int, choices=[16, 32, 64, 128, 256], default=128)
    parser.add_argument('--samples', type=int, default=256)
    args = parser.parse_args()
    if args.samples < 64 or args.samples > 4096:
        parser.error('samples must be between 64 and 4096')
    source = studio
    if args.input:
        image = np.load(args.input, allow_pickle=False)
        if image.ndim != 3 or image.shape[2] != 3 or min(image.shape[:2]) < 2 or not np.isfinite(image).all() or np.min(image) < 0 or np.max(image) > 65504:
            parser.error('input must be a finite nonnegative H×W×3 linear RGB array that fits float16')
        source = image_source(image)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bake(source, args.size, args.lut_size, args.samples))
    print(f'Wrote {args.output}: {args.output.stat().st_size} bytes')
