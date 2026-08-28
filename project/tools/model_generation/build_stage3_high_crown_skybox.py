"""画像生成した全天周画像からステージ3専用のCubeMap DDSを作成します。"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = PROJECT_ROOT / "Resources" / "skybox" / "stage3_high_crown_source.png"
REFERENCE_DDS_PATH = PROJECT_ROOT / "Resources" / "output_skybox.dds"
OUTPUT_PATH = PROJECT_ROOT / "Resources" / "skybox" / "stage3_high_crown.dds"
TEXCONV_PATH = PROJECT_ROOT / "Resources" / "tools" / "Texconv.exe"
FACE_SIZE = 1024
FACE_NAMES = ("positive_x", "negative_x", "positive_y", "negative_y", "positive_z", "negative_z")


def asset_guid(path: Path) -> str:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    return hashlib.md5(f"cg2:stage3-high-crown-skybox:{relative}".encode("utf-8")).hexdigest()


def read_source() -> np.ndarray:
    if not SOURCE_PATH.is_file():
        raise RuntimeError(f"Skybox元画像が見つかりません: {SOURCE_PATH}")
    image = Image.open(SOURCE_PATH).convert("RGB")
    width, height = image.size
    if width < 1024 or height < 512 or abs(width / height - 2.0) > 0.03:
        raise RuntimeError(f"元画像は2:1の全天周画像である必要があります: {width}x{height}")
    return np.asarray(image, dtype=np.float32) / 255.0


def face_directions(face_index: int, size: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    axis = (np.arange(size, dtype=np.float32) + 0.5) / size * 2.0 - 1.0
    u, v = np.meshgrid(axis, axis)
    if face_index == 0:
        x, y, z = np.ones_like(u), -v, -u
    elif face_index == 1:
        x, y, z = -np.ones_like(u), -v, u
    elif face_index == 2:
        x, y, z = u, np.ones_like(u), v
    elif face_index == 3:
        x, y, z = u, -np.ones_like(u), -v
    elif face_index == 4:
        x, y, z = u, -v, np.ones_like(u)
    else:
        x, y, z = -u, -v, -np.ones_like(u)
    length = np.sqrt(x * x + y * y + z * z)
    return x / length, y / length, z / length


def bilinear_sample_panorama(source: np.ndarray, x: np.ndarray, y: np.ndarray, z: np.ndarray) -> np.ndarray:
    height, width, _ = source.shape
    longitude = np.arctan2(z, x)
    latitude = np.arcsin(np.clip(y, -1.0, 1.0))
    source_x = (longitude / (2.0 * math.pi) + 0.5) * width - 0.5
    source_y = (0.5 - latitude / math.pi) * height - 0.5

    x0 = np.floor(source_x).astype(np.int32)
    y0 = np.floor(source_y).astype(np.int32)
    x1 = (x0 + 1) % width
    y1 = np.clip(y0 + 1, 0, height - 1)
    x0 %= width
    y0 = np.clip(y0, 0, height - 1)

    weight_x = (source_x - np.floor(source_x))[..., None]
    weight_y = (source_y - np.floor(source_y))[..., None]
    top = source[y0, x0] * (1.0 - weight_x) + source[y0, x1] * weight_x
    bottom = source[y1, x0] * (1.0 - weight_x) + source[y1, x1] * weight_x
    return top * (1.0 - weight_y) + bottom * weight_y


def srgb_to_linear(color: np.ndarray) -> np.ndarray:
    return np.where(color <= 0.04045, color / 12.92, np.power((color + 0.055) / 1.055, 2.4))


def build_faces(source: np.ndarray) -> list[np.ndarray]:
    faces: list[np.ndarray] = []
    for face_index in range(6):
        x, y, z = face_directions(face_index, FACE_SIZE)
        srgb = bilinear_sample_panorama(source, x, y, z)
        linear = np.clip(srgb_to_linear(srgb) * 1.22, 0.0, 8.0)
        alpha = np.ones((FACE_SIZE, FACE_SIZE, 1), dtype=np.float32)
        faces.append(np.concatenate((linear, alpha), axis=2))
    return faces


def write_uncompressed_cubemap(path: Path, faces: list[np.ndarray]) -> None:
    header = bytearray(REFERENCE_DDS_PATH.read_bytes()[:148])
    if header[:4] != b"DDS " or header[84:88] != b"DX10":
        raise RuntimeError("参照DDSがDX10形式ではありません。")
    width = struct.unpack_from("<I", header, 16)[0]
    height = struct.unpack_from("<I", header, 12)[0]
    dxgi_format = struct.unpack_from("<I", header, 128)[0]
    if width != FACE_SIZE or height != FACE_SIZE or dxgi_format != 10:
        raise RuntimeError(f"参照DDSの形式が想定外です: {width}x{height}, DXGI={dxgi_format}")
    with path.open("wb") as stream:
        stream.write(header)
        for face in faces:
            stream.write(np.asarray(face, dtype="<f2").tobytes(order="C"))


def convert_to_bc6h(source_dds: Path) -> None:
    if not TEXCONV_PATH.is_file():
        raise RuntimeError(f"Texconv.exeが見つかりません: {TEXCONV_PATH}")
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [str(TEXCONV_PATH), "-f", "BC6H_UF16", "-y", "-m", "11", "-o", str(OUTPUT_PATH.parent), str(source_dds)],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(f"SkyboxのDDS変換に失敗しました。\n{completed.stdout}\n{completed.stderr}")


def write_meta(path: Path) -> None:
    payload = {
        "assetType": "Texture",
        "guid": asset_guid(path),
        "importSettings": {"colorSpace": "Linear" if path.suffix.lower() == ".dds" else "sRGB"},
        "importer": "TextureImporter",
        "source": path.relative_to(PROJECT_ROOT).as_posix(),
        "version": 1,
    }
    Path(str(path) + ".meta").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_preview(path: Path, faces: list[np.ndarray]) -> None:
    width = 384
    sheet = Image.new("RGB", (width * 3, width * 2), (24, 32, 52))
    draw = ImageDraw.Draw(sheet)
    for index, face in enumerate(faces):
        mapped = face[:, :, :3] / (1.0 + face[:, :, :3])
        mapped = np.power(np.clip(mapped, 0.0, 1.0), 1.0 / 2.2)
        image = Image.fromarray((mapped * 255.0).astype(np.uint8), "RGB")
        image.thumbnail((width, width))
        x = index % 3 * width
        y = index // 3 * width
        sheet.paste(image, (x, y))
        draw.text((x + 8, y + 8), FACE_NAMES[index], fill=(255, 255, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def validate() -> None:
    data = OUTPUT_PATH.read_bytes()
    if data[:4] != b"DDS " or data[84:88] != b"DX10":
        raise RuntimeError("生成したSkybox DDSが不正です。")
    dxgi_format = struct.unpack_from("<I", data, 128)[0]
    misc_flag = struct.unpack_from("<I", data, 136)[0]
    mip_count = struct.unpack_from("<I", data, 28)[0]
    if dxgi_format != 95 or not (misc_flag & 4) or mip_count != 11:
        raise RuntimeError(f"生成したSkybox属性が不正です: DXGI={dxgi_format}, misc={misc_flag}, mips={mip_count}")


def main() -> None:
    parser = argparse.ArgumentParser(description="ステージ3 High CrownのSkyboxを生成します。")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    source = read_source()
    faces = build_faces(source)
    with tempfile.TemporaryDirectory(prefix="cg2_stage3_skybox_") as temp_dir:
        uncompressed = Path(temp_dir) / OUTPUT_PATH.name
        write_uncompressed_cubemap(uncompressed, faces)
        convert_to_bc6h(uncompressed)
    write_meta(SOURCE_PATH)
    write_meta(OUTPUT_PATH)
    validate()
    if args.preview:
        write_preview(args.preview, faces)
    print(f"Generated Stage 3 skybox: {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
