from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


def smooth_wrapped_seam(image: Image.Image) -> Image.Image:
    source = np.asarray(image.convert("RGB"))
    shifted = np.roll(source, source.shape[1] // 2, axis=1)
    shifted_image = Image.fromarray(shifted, mode="RGB")

    width, height = shifted_image.size
    blend_width = max(32, width // 24)
    center = width // 2
    mask = np.zeros((height, width), dtype=np.uint8)
    distances = np.abs(np.arange(width) - center)
    weights = np.clip(1.0 - distances / float(blend_width), 0.0, 1.0)
    weights = weights * weights * (3.0 - 2.0 * weights)
    mask[:, :] = np.round(weights * 255.0).astype(np.uint8)[None, :]

    softened = shifted_image.filter(ImageFilter.GaussianBlur(radius=10.0))
    return Image.composite(softened, shifted_image, Image.fromarray(mask, mode="L"))


def sample_equirectangular(panorama: np.ndarray, direction: np.ndarray) -> np.ndarray:
    height, width, _ = panorama.shape
    length = np.linalg.norm(direction, axis=-1, keepdims=True)
    normalized = direction / np.maximum(length, 1.0e-8)

    longitude = np.arctan2(normalized[..., 2], normalized[..., 0])
    latitude = np.arcsin(np.clip(normalized[..., 1], -1.0, 1.0))
    source_x = (longitude / (2.0 * math.pi) + 0.5) * width - 0.5
    source_y = (0.5 - latitude / math.pi) * height - 0.5

    x0 = np.floor(source_x).astype(np.int32) % width
    y0 = np.clip(np.floor(source_y).astype(np.int32), 0, height - 1)
    x1 = (x0 + 1) % width
    y1 = np.clip(y0 + 1, 0, height - 1)
    tx = (source_x - np.floor(source_x))[..., None]
    ty = (source_y - np.floor(source_y))[..., None]

    top = panorama[y0, x0] * (1.0 - tx) + panorama[y0, x1] * tx
    bottom = panorama[y1, x0] * (1.0 - tx) + panorama[y1, x1] * tx
    return np.clip(top * (1.0 - ty) + bottom * ty, 0.0, 255.0).astype(np.uint8)


def build_face_directions(face_name: str, size: int) -> np.ndarray:
    coordinate = (np.arange(size, dtype=np.float32) + 0.5) * (2.0 / size) - 1.0
    u, v = np.meshgrid(coordinate, coordinate)

    if face_name == "positive_x":
        return np.stack((np.ones_like(u), -v, -u), axis=-1)
    if face_name == "negative_x":
        return np.stack((-np.ones_like(u), -v, u), axis=-1)
    if face_name == "positive_y":
        return np.stack((u, np.ones_like(u), v), axis=-1)
    if face_name == "negative_y":
        return np.stack((u, -np.ones_like(u), -v), axis=-1)
    if face_name == "positive_z":
        return np.stack((u, -v, np.ones_like(u)), axis=-1)
    if face_name == "negative_z":
        return np.stack((-u, -v, -np.ones_like(u)), axis=-1)
    raise ValueError(f"Unknown cubemap face: {face_name}")


def write_dx10_cubemap(path: Path, faces: list[np.ndarray]) -> None:
    size = faces[0].shape[0]
    ddsd_caps = 0x00000001
    ddsd_height = 0x00000002
    ddsd_width = 0x00000004
    ddsd_pitch = 0x00000008
    ddsd_pixel_format = 0x00001000
    ddpf_fourcc = 0x00000004
    dds_caps_complex = 0x00000008
    dds_caps_texture = 0x00001000
    dds_caps2_cubemap_all_faces = 0x0000FE00

    header_values = [
        124,
        ddsd_caps | ddsd_height | ddsd_width | ddsd_pitch | ddsd_pixel_format,
        size,
        size,
        size * 4,
        0,
        0,
        *([0] * 11),
        32,
        ddpf_fourcc,
        int.from_bytes(b"DX10", "little"),
        0,
        0,
        0,
        0,
        0,
        dds_caps_texture | dds_caps_complex,
        dds_caps2_cubemap_all_faces,
        0,
        0,
        0,
    ]

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(b"DDS ")
        output.write(struct.pack("<31I", *header_values))
        output.write(struct.pack("<5I", 28, 3, 4, 1, 0))
        for face in faces:
            alpha = np.full((size, size, 1), 255, dtype=np.uint8)
            output.write(np.concatenate((face, alpha), axis=-1).tobytes())


def save_cross_preview(path: Path, faces: dict[str, np.ndarray]) -> None:
    size = next(iter(faces.values())).shape[0]
    preview = Image.new("RGB", (size * 4, size * 3), (16, 12, 24))
    placements = {
        "positive_y": (size, 0),
        "negative_x": (0, size),
        "positive_z": (size, size),
        "positive_x": (size * 2, size),
        "negative_z": (size * 3, size),
        "negative_y": (size, size * 2),
    }
    for name, position in placements.items():
        preview.paste(Image.fromarray(faces[name], mode="RGB"), position)
    path.parent.mkdir(parents=True, exist_ok=True)
    preview.save(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--face-size", type=int, default=512)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    panorama_image = smooth_wrapped_seam(Image.open(args.input))
    panorama = np.asarray(panorama_image, dtype=np.float32)
    face_order = [
        "positive_x",
        "negative_x",
        "positive_y",
        "negative_y",
        "positive_z",
        "negative_z",
    ]
    face_map = {
        name: sample_equirectangular(
            panorama,
            build_face_directions(name, args.face_size),
        )
        for name in face_order
    }
    write_dx10_cubemap(args.output, [face_map[name] for name in face_order])
    if args.preview:
        save_cross_preview(args.preview, face_map)


if __name__ == "__main__":
    main()
