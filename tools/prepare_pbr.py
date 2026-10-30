"""SPDX-License-Identifier: MIT
Generate original seamless PBR materials and GGX-prefiltered HDR lighting probes.
Requires NumPy only at authoring time. No downloads, photographs or external textures.
"""
from pathlib import Path
import argparse
import hashlib
import struct

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/pbr"
BAKE_SOURCES = ['src/scene.cpp', 'include/cyvr/scene.hpp', 'src/pbr.cpp', 'tools/bake_scene_lighting.cpp']

def source_digest(name):
    # Checkouts may use either line ending; only the source text decides whether a bake is stale.
    return hashlib.sha256((ROOT / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest()

def record_checksums():
    files = sorted(OUTPUT.glob('*.bin'))
    (OUTPUT / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files), encoding='utf-8')

def normalize(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)

def srgb(v):
    return np.where(v <= .0031308, v * 12.92, 1.055 * np.maximum(v, 0) ** (1 / 2.4) - .055)

def encode(levels, fmt):
    layers, height, width, _ = levels[0].shape
    header = b"CYTX" + struct.pack("<7I", 1, width, height, layers, len(levels), fmt, int(layers == 6))
    chunks = []
    for a in levels:
        if fmt == 1:
            a = a.copy()
            a[..., :3] = srgb(a[..., :3])
        if fmt <= 2:
            chunks.append(np.rint(np.clip(a, 0, 1) * 255).astype('u1').tobytes())
        else:
            chunks.append(a.astype('<f2').tobytes())
    return header + b''.join(chunks)

def material_maps():
    size = 128
    v, u = np.mgrid[0:size, 0:size] / size
    tau = 2 * np.pi
    colors, surfaces = [], []
    # Heights are periodic; texture repeats have no discontinuity at their boundaries.
    for material in range(9):
        fine = np.sin(tau * (u * 23 + v * 17)) * np.sin(tau * (u * 13 - v * 29))
        height = fine * .001
        color = np.ones_like(u)
        rough = np.full_like(u, .85)
        metal = np.zeros_like(u)
        ao = np.ones_like(u)
        if material == 1:  # wood: longitudinal grain and larger growth bands
            grain = np.sin(tau * (u * 15 + .25 * np.sin(tau * v) + .10 * np.sin(tau * v * 3)))
            bands = np.sin(tau * (u * 3 + .12 * np.sin(tau * v)))
            color = .84 + .055 * bands + .035 * grain + .018 * fine
            height = .00014 * grain + .00010 * bands
            rough = .40 + .10 * (.5 + .5 * grain)
        elif material == 2:  # woven cloth
            warp, weft = np.sin(tau * u * 32), np.sin(tau * v * 32)
            height = .0018 * (warp + weft) + .0008 * warp * weft
            color = .86 + .07 * warp * weft + .025 * fine
            rough = .89 + .07 * warp * weft
            ao = .92 + .08 * (.5 + .5 * warp * weft)
        elif material == 3:  # brushed metal
            brush = np.sin(tau * (u * 49 + .10 * np.sin(tau * v * 2)))
            height = .00007 * brush
            color = .94 + .04 * fine
            rough = .21 + .07 * (.5 + .5 * brush)
            metal[:] = 1
        elif material == 5:  # static water micro-normal; no animated normal swimming in VR
            height = .012 * np.sin(tau * (u * 2 + v)) + .004 * np.cos(tau * (u - v * 3))
            color[:] = 1
            rough[:] = .12
        elif material == 6:
            height = .0015 * np.cos(tau * u * 8)
            color = .82 + .12 * np.cos(tau * u * 8) + .035 * fine
            rough[:] = .73
        elif material == 7:
            height = .00055 * fine
            color = .93 + .04 * fine
            rough = .22 + .05 * (.5 + .5 * fine)
        elif material == 8:
            height[:] = 0
            rough[:] = .94
        dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * size * .5
        dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * size * .5
        n = normalize(np.stack((-dx, -dy, np.ones_like(u)), -1))
        colors.append(np.stack((color, color, color, ao), -1))
        surfaces.append(np.stack((n[..., 0] * .5 + .5, n[..., 1] * .5 + .5, rough, metal), -1))
    color_levels, surface_levels = [np.stack(colors)], [np.stack(surfaces)]
    while color_levels[-1].shape[1] > 1:
        def box(a):
            return (a[:, ::2, ::2] + a[:, 1::2, ::2] + a[:, ::2, 1::2] + a[:, 1::2, 1::2]) * .25
        c, a = box(color_levels[-1]), box(surface_levels[-1])
        # Unresolved normal variance widens the specular lobe in distant mips.
        previous = surface_levels[-1]
        variance = box((previous[..., :2] * 2 - 1) ** 2).sum(-1) - ((a[..., :2] * 2 - 1) ** 2).sum(-1)
        a[..., 2] = np.sqrt(np.minimum(1, a[..., 2] ** 2 + np.maximum(variance, 0)))
        color_levels.append(c)
        surface_levels.append(a)
    return encode(color_levels, 1), encode(surface_levels, 2)

def sky(kind, d):
    h = np.clip(d[..., 1:2], 0, 1)
    g = np.clip(-d[..., 1:2] * 3, 0, 1)
    if kind == 4:
        horizon, top, bottom = (.075, .12, .22), (.018, .035, .09), (.06, .09, .12)
    elif kind == 5:
        horizon, top, bottom = (.75, .40, .22), (.16, .32, .58), (.18, .12, .08)
    else:
        horizon, top, bottom = (.66, .74, .82), (.25, .48, .78), (.19, .15, .10)
    result = ((1 - h) * np.array(horizon) + h * np.array(top)) * (1 - g) + g * np.array(bottom)
    if kind == 3:
        # A centre-of-room probe: window openings, pale ceiling and warm floor.
        front = d[..., 2:3] < -.55
        ceiling = d[..., 1:2] > .55
        wall = np.array((.24, .22, .18))
        result = np.where(front, result * 1.8, result * .28 + wall)
        result = np.where(ceiling, np.array((.64, .59, .49)), result)
        for x in (-.62, .62):
            lamp = normalize(np.array((x, .55, -.4)))
            result += np.maximum(np.sum(d * lamp, -1, keepdims=True), 0) ** 180 * np.array((3.5, 2.4, 1.2))
    elif kind == 4:
        for x in (-.75, .75):
            strip = normalize(np.array((x, .25, -.7)))
            result += np.maximum(np.sum(d * strip, -1, keepdims=True), 0) ** 65 * np.array((.6, 2.0, 3.7))
    # The directional sun is evaluated separately by the BRDF, avoiding double counting.
    return result

def samples(count):
    # Hammersley sequence, deterministic integer bit reversal.
    i = np.arange(count, dtype=np.uint32)
    v = i.copy()
    v = (v << 16) | (v >> 16)
    v = ((v & 0x55555555) << 1) | ((v & 0xAAAAAAAA) >> 1)
    v = ((v & 0x33333333) << 2) | ((v & 0xCCCCCCCC) >> 2)
    v = ((v & 0x0F0F0F0F) << 4) | ((v & 0xF0F0F0F0) >> 4)
    v = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8)
    return np.stack((i / count, v.astype(np.float64) / 2**32), -1)

def ggx(xi, rough):
    a = rough * rough
    phi = xi[..., 0] * 2 * np.pi
    z = np.sqrt((1 - xi[..., 1]) / np.maximum(1 + (a * a - 1) * xi[..., 1], 1e-9))
    r = np.sqrt(np.maximum(0, 1 - z * z))
    return np.stack((r * np.cos(phi), r * np.sin(phi), z), -1)

def cube_directions(size):
    y, x = np.mgrid[0:size, 0:size]
    u, v = 2 * (x + .5) / size - 1, 2 * (y + .5) / size - 1
    one = np.ones_like(u)
    # Vulkan cube face order: +X, -X, +Y, -Y, +Z, -Z.
    faces = [(one, -v, -u), (-one, -v, u), (u, one, v), (u, -one, -v), (u, -v, one), (-u, -v, -one)]
    return normalize(np.stack([np.stack(f, -1) for f in faces]))

def probe(kind):
    levels = []
    xi = samples(256)
    for level in range(8):
        size = max(1, 128 >> level)
        dirs = cube_directions(size)
        flat = dirs.reshape(-1, 3)
        if level == 0:
            value = sky(kind, flat)
        else:
            value = np.zeros_like(flat)
            weight = np.zeros((len(flat), 1))
            up = np.where((np.abs(flat[:, 2]) < .99)[:, None], (0, 0, 1), (1, 0, 0))
            t = normalize(np.cross(up, flat))
            b = np.cross(flat, t)
            for h in ggx(xi, level / 7):
                world = t * h[0] + b * h[1] + flat * h[2]
                direction = normalize(2 * np.sum(world * flat, -1, keepdims=True) * world - flat)
                w = np.maximum(np.sum(direction * flat, -1, keepdims=True), 0)
                value += sky(kind, direction) * w
                weight += w
            value /= np.maximum(weight, 1e-9)
        value = value.reshape(6, size, size, 3)
        levels.append(np.concatenate((value, np.ones((6, size, size, 1))), -1))
    return encode(levels, 3)

def brdf_lut():
    size = 64
    rough, nov = np.mgrid[0:size, 0:size]
    rough, nov = (rough + .5) / size, (nov + .5) / size
    v = np.stack((np.sqrt(1 - nov * nov), np.zeros_like(nov), nov), -1)
    output = np.zeros((size, size, 2))
    count = 512
    for xi in samples(count):
        h = ggx(np.broadcast_to(xi, (size, size, 2)), rough)
        voh = np.maximum(np.sum(v * h, -1), 0)
        l = 2 * voh[..., None] * h - v
        nol = np.maximum(l[..., 2], 0)
        noh = np.maximum(h[..., 2], 1e-6)
        a2 = rough**4
        denominator = nol * np.sqrt(nov * nov * (1 - a2) + a2) + nov * np.sqrt(nol * nol * (1 - a2) + a2)
        visibility = .5 / np.maximum(denominator, 1e-9)
        weight = 4 * visibility * nol * voh / noh
        f = (1 - voh)**5
        output[..., 0] += (1 - f) * weight / count
        output[..., 1] += f * weight / count
    return encode([output[None]], 4)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify the checked-in texture checksums without rebaking')
    parser.add_argument('--record-lighting', action='store_true', help='Record newly baked lighting and its source fingerprints')
    args = parser.parse_args()
    if args.record_lighting:
        for scene, name in ((3, 'lounge'), (4, 'observatory'), (5, 'terrace')):
            data = (OUTPUT / f'lighting-{name}.bin').read_bytes()
            if data[:4] != b'CYLI' or struct.unpack('<I', data[8:12])[0] != scene:
                raise SystemExit(f'Invalid lighting resource: {name}')
        (OUTPUT / 'BAKE_SOURCES.sha256').write_text(''.join(
            f'{source_digest(name)}  {name}\n' for name in BAKE_SOURCES), encoding='utf-8')
        record_checksums()
        print('Lighting asset and source checksums recorded')
        return
    if args.check:
        for line in (OUTPUT / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ', 1)
            if hashlib.sha256((OUTPUT / name).read_bytes()).hexdigest() != digest:
                raise SystemExit(f'PBR checksum mismatch: {name}')
        sources = OUTPUT / 'BAKE_SOURCES.sha256'
        if sources.exists():
            for line in sources.read_text().splitlines():
                digest, name = line.split('  ', 1)
                if source_digest(name) != digest:
                    raise SystemExit(f'Lighting bake is stale after changing {name}. Run tools/bake-lighting.ps1.')
        print('PBR asset checksums verified')
        return
    global np
    import numpy as np
    OUTPUT.mkdir(parents=True, exist_ok=True)
    color, surface = material_maps()
    files = {'basecolor.bin': color, 'normal-rough-metal.bin': surface, 'brdf.bin': brdf_lut()}
    for kind, name in ((3, 'lounge'), (4, 'observatory'), (5, 'terrace')):
        print(f'Baking {name} HDR probe', flush=True)
        files[f'{name}.bin'] = probe(kind)
    for name, data in files.items():
        (OUTPUT / name).write_bytes(data)
        print(f'{name}: {len(data):,} bytes')
    record_checksums()

if __name__ == '__main__':
    main()
