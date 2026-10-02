"""Plain CPython (Pillow + numpy) — NOT a UE script. Run with the system `python` on PATH.

Derives small, mobile-ready PNG sets from the three 4K Meshy pocket-watch texture exports
(design.md §3.1) so the UE importer (Scripts/import_time_watch.py) never has to touch the
original 4K/2K PNGs directly. Originals under TimeMachineAR/Asset/Clock/ are only ever read.

Output (docs/ai-handoff/inbox/clock/), LANCZOS resample:
    T_Watch_Body_BaseColor.png    2048   RGB   (from body *_image-to-3d-texture.png)
    T_Watch_Body_Normal.png       2048   RGB   (from body *_normal.png, no channel edits)
    T_Watch_Body_MR.png           1024   RGB   R=metallic, G=roughness, B=0 (packed mask)
    T_Watch_HandHour_BaseColor.png   1024
    T_Watch_HandHour_Normal.png      1024
    T_Watch_HandHour_MR.png          512
    T_Watch_HandMinute_BaseColor.png 1024
    T_Watch_HandMinute_Normal.png    1024
    T_Watch_HandMinute_MR.png        512

Idempotent: re-running overwrites the same 9 files with the same content (deterministic resize).
"""
from pathlib import Path

import numpy as np
from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parents[1]          # TimeMachineAR/
SOURCE_ROOT = PROJECT_ROOT / "Asset/Clock"
DEST_ROOT = PROJECT_ROOT.parent / "docs/ai-handoff/inbox/clock"

# folder name -> (output prefix, BaseColor/Normal size, MR size)
PARTS = {
    "Meshy_AI_pocket_watch_body_0914084005_image-to-3d-texture_fbx": ("Watch_Body", 2048, 1024),
    "Meshy_AI_pocket_watch_hour_han_0914084753_image-to-3d-texture_fbx": ("Watch_HandHour", 1024, 512),
    "Meshy_AI_pocket_watch_minute_h_0914084700_image-to-3d-texture_fbx": ("Watch_HandMinute", 1024, 512),
}


def find_one(folder: Path, suffix: str) -> Path:
    hits = list(folder.rglob("*" + suffix))
    if not hits:
        raise RuntimeError(f"missing source PNG '*{suffix}' under {folder}")
    if len(hits) > 1:
        raise RuntimeError(f"ambiguous source PNG '*{suffix}' under {folder}: {hits}")
    return hits[0]


def resize(im: Image.Image, size: int) -> Image.Image:
    return im.resize((size, size), Image.LANCZOS)


def derive_part(folder_name: str, prefix: str, colour_size: int, mr_size: int) -> None:
    folder = SOURCE_ROOT / folder_name
    base_src = find_one(folder, "image-to-3d-texture.png")           # BaseColor (not _normal/_metallic/_roughness)
    normal_src = find_one(folder, "_normal.png")
    metallic_src = find_one(folder, "_metallic.png")
    roughness_src = find_one(folder, "_roughness.png")

    # BaseColor
    base = Image.open(base_src).convert("RGB")
    base_out = resize(base, colour_size)
    base_path = DEST_ROOT / f"T_{prefix}_BaseColor.png"
    base_out.save(base_path)
    print(f"DERIVE_CLOCK {base_path.name} {base_out.size[0]}x{base_out.size[1]}")

    # Normal — resized only, no green-channel flip (design.md §3.1: decided after lighting review).
    normal = Image.open(normal_src).convert("RGB")
    normal_out = resize(normal, colour_size)
    normal_path = DEST_ROOT / f"T_{prefix}_Normal.png"
    normal_out.save(normal_path)
    print(f"DERIVE_CLOCK {normal_path.name} {normal_out.size[0]}x{normal_out.size[1]}")

    # MR packing: R=metallic, G=roughness, B=0. Resize each source mask independently at full
    # resolution first (LANCZOS), THEN pack, so the two masks are not cross-contaminated by
    # resizing an already-packed image.
    metallic = Image.open(metallic_src).convert("L")
    roughness = Image.open(roughness_src).convert("L")
    metallic_r = np.asarray(resize(metallic, mr_size), dtype=np.uint8)
    roughness_r = np.asarray(resize(roughness, mr_size), dtype=np.uint8)
    zeros = np.zeros_like(metallic_r)
    mr = np.stack([metallic_r, roughness_r, zeros], axis=-1)
    mr_out = Image.fromarray(mr, mode="RGB")
    mr_path = DEST_ROOT / f"T_{prefix}_MR.png"
    mr_out.save(mr_path)
    print(f"DERIVE_CLOCK {mr_path.name} {mr_out.size[0]}x{mr_out.size[1]}")


def main() -> None:
    DEST_ROOT.mkdir(parents=True, exist_ok=True)
    for folder_name, (prefix, colour_size, mr_size) in PARTS.items():
        derive_part(folder_name, prefix, colour_size, mr_size)
    print("DERIVE_CLOCK_DONE 9 files ->", DEST_ROOT)


if __name__ == "__main__":
    main()
