#!/usr/bin/env python3
import pathlib

import numpy as np
from PIL import Image

SIZE = 1024
OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "resources" / "textures" / "pbr"


def spectral_noise(seed, low_cycles, high_cycles, falloff):
    generator = np.random.default_rng(seed)
    white = generator.standard_normal((SIZE, SIZE))
    frequency = np.fft.fftfreq(SIZE) * SIZE
    radius = np.hypot(frequency[:, None], frequency[None, :])
    band = (radius >= low_cycles) & (radius <= high_cycles)
    gain = np.where(band, np.maximum(radius, 1.0) ** -falloff, 0.0)
    field = np.fft.ifft2(np.fft.fft2(white) * gain).real
    field -= field.mean()
    return np.clip(field / (3.0 * field.std() + 1e-9) * 0.5 + 0.5, 0.0, 1.0)


def smoothstep(low, high, value):
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def blocks(seed, columns, rows, stagger, joint, bevel, warp):
    v, u = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    u = u + (spectral_noise(seed + 1, 3, 24, 1.2) - 0.5) * warp
    v = v + (spectral_noise(seed + 2, 3, 24, 1.2) - 0.5) * warp
    row = np.floor(v * rows).astype(int) % rows
    shifted = u * columns + stagger * (row % 2)
    column = np.floor(shifted).astype(int) % columns
    across = np.abs(shifted - np.floor(shifted) - 0.5)
    down = np.abs(v * rows - np.floor(v * rows) - 0.5)
    edge = np.minimum((0.5 - across) / columns, (0.5 - down) / rows)
    face = smoothstep(joint, joint + bevel, edge)
    shade = np.random.default_rng(seed + 3).random((rows, columns))[row, column]
    return face, shade


def normal_map(height, relief):
    texels = relief * SIZE
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * texels
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * texels
    normal = np.dstack([-dx, -dy, np.ones_like(height)])
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return normal * 0.5 + 0.5


def cavity(height, radius_cycles, strength):
    frequency = np.fft.fftfreq(SIZE) * SIZE
    radius = np.hypot(frequency[:, None], frequency[None, :])
    blurred = np.fft.ifft2(np.fft.fft2(height) * np.exp(-((radius / radius_cycles) ** 2))).real
    return np.clip(1.0 - np.maximum(blurred - height, 0.0) * strength, 0.35, 1.0)


def store(image, name):
    Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB").save(OUTPUT / name, optimize=True)


def emit(name, albedo, height, roughness, relief):
    if albedo.ndim == 2:
        albedo = np.dstack([albedo] * 3)
    store(albedo, f"{name}_albedo.png")
    store(normal_map(height, relief), f"{name}_normal.png")
    store(np.dstack([cavity(height, 40.0, 2.5), roughness, np.ones_like(height)]), f"{name}_orm.png")


def plaster():
    broad = spectral_noise(10, 2, 12, 1.4)
    grain = spectral_noise(11, 24, 220, 0.7)
    height = broad * 0.7 + grain * 0.2
    albedo = 0.94 + (broad - 0.5) * 0.07 + (grain - 0.5) * 0.04
    emit("plaster", albedo, height, 0.92 + (grain - 0.5) * 0.12, 0.0025)


def masonry(name, seed, columns, rows, stagger, polish):
    face, shade = blocks(seed, columns, rows, stagger, 0.0025, 0.012, 0.012)
    broad = spectral_noise(seed + 10, 3, 20, 1.2)
    grain = spectral_noise(seed + 11, 20, 260, 0.6)
    height = face * (0.8 + broad * 0.35 + grain * 0.12) + (1.0 - face) * grain * 0.15
    stone = 0.68 + shade * 0.3 + (broad - 0.5) * 0.28 + (grain - 0.5) * 0.18
    mortar = 0.42 + (grain - 0.5) * 0.2
    albedo = mortar + (stone - mortar) * face
    roughness = 1.0 - face * polish * (0.6 + broad * 0.4) + (grain - 0.5) * 0.12
    emit(name, albedo, height, roughness, 0.02)


def tiles():
    face, shade = blocks(40, 4, 4, 0.0, 0.0018, 0.004, 0.0015)
    cloud = spectral_noise(41, 2, 40, 1.3)
    vein = 1.0 - smoothstep(0.0, 0.07, np.abs(spectral_noise(42, 3, 30, 1.5) - 0.5))
    grain = spectral_noise(43, 40, 300, 0.5)
    height = face + grain * 0.01
    stone = 0.76 + shade * 0.12 + (cloud - 0.5) * 0.18 + vein * 0.07
    albedo = 0.45 + (stone - 0.45) * face
    roughness = 1.0 - face * (0.72 - cloud * 0.22 - vein * 0.05)
    emit("tiles", albedo, height, roughness, 0.004)


def rock():
    broad = spectral_noise(50, 2, 14, 1.5)
    ridges = 1.0 - np.abs(spectral_noise(51, 5, 60, 1.2) * 2.0 - 1.0)
    grain = spectral_noise(52, 40, 280, 0.6)
    height = broad * 0.8 + ridges ** 3 * 0.35 + grain * 0.1
    albedo = 0.6 + (broad - 0.5) * 0.45 + (ridges - 0.7) * 0.2 + (grain - 0.5) * 0.22
    emit("rock", albedo, height, 0.9 + (grain - 0.5) * 0.2, 0.035)


def wood():
    v, u = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    face, shade = blocks(60, 1, 6, 0.0, 0.002, 0.006, 0.002)
    warp = spectral_noise(61, 1, 10, 1.5)
    rings = 0.5 + 0.5 * np.sin(2.0 * np.pi * (v * 54.0 + warp * 7.0 + shade * 11.0))
    streak = np.repeat(spectral_noise(62, 8, 200, 0.6)[:, ::8], 8, axis=1)
    grain = spectral_noise(63, 60, 300, 0.4)
    height = face * (0.85 + rings * 0.06 + grain * 0.05)
    albedo = 0.3 + (0.3 + shade * 0.22 + rings * 0.2 + (streak - 0.5) * 0.25 + (grain - 0.5) * 0.1) * face
    roughness = 0.82 + rings * 0.12 + (1.0 - face) * 0.2
    emit("wood", albedo, height, roughness, 0.012)


def ground():
    broad = spectral_noise(70, 3, 24, 1.1)
    clumps = spectral_noise(71, 24, 90, 0.5)
    blades = spectral_noise(72, 90, 360, 0.3)
    height = broad * 0.4 + clumps * 0.4 + blades * 0.25
    tone = 0.62 + (broad - 0.5) * 0.4 + (clumps - 0.5) * 0.35 + (blades - 0.5) * 0.4
    dry = smoothstep(0.55, 0.8, spectral_noise(73, 2, 16, 1.2))
    albedo = np.dstack([tone * (0.82 + dry * 0.35), tone * (1.0 + dry * 0.02), tone * (0.62 - dry * 0.08)])
    emit("ground", albedo, height, 0.95 + (blades - 0.5) * 0.1, 0.02)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    plaster()
    masonry("masonry", 20, 4, 8, 0.5, 0.18)
    masonry("flagstone", 30, 4, 4, 0.37, 0.42)
    tiles()
    rock()
    wood()
    ground()


if __name__ == "__main__":
    main()
