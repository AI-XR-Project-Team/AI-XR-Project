"""Cut single footprints / ring / chevron / glow out of the golden footprint sheet.

Plain CPython (Pillow + numpy), NOT UE Python. Run from anywhere:

    python TimeMachineAR/Scripts/extract_nav_footprints.py

Input : docs/ai-handoff/inbox/nav_footprints/src/02_footprint_asset_sheet.png  (kept untouched)
Output: docs/ai-handoff/inbox/nav_footprints/
          T_Footprint_Left.png / T_Footprint_Right.png   512x512, toes point to row 0 (+V up)
          T_Footprint_Glow.png                            256x256 radial amber glow (from "글로우 풀")
          T_NavArrivalRing.png                            512x512 circle ring (from "도착 링" profile)
          T_NavForwardChevron.png                         128x128 double chevron
          crop_manifest.json                              rects, transforms, notes
          verify/*.png                                    crops composited on light / dark / checker

Why the extra work instead of a plain crop:
  * The sheet bakes an amber glow into semi-transparent pixels, and neighbouring
    footprints' glows overlap in the gaps. A distance-from-core attenuation keeps our
    own glow (alpha ~4 at 18 px) and removes the neighbours' tails.
  * Pixels with alpha < ~30 carry garbage RGB (pure red / yellow). Unpremultiplied
    export artefact — invisible in a viewer, but ASTC + mip generation would bleed it
    into a red fringe on a bright floor. Low-alpha RGB is replaced with the measured
    glow colour.
  * Ring and glow pool are drawn in perspective (ellipses) with the sheet label 6-7 px
    under them. Instead of stretching a label-cut ellipse we sample the clean horizontal
    radial profile and regenerate a true circle from it.
"""
from __future__ import annotations

import json
import math
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
INBOX = ROOT / "docs/ai-handoff/inbox/nav_footprints"
SRC = INBOX / "src/02_footprint_asset_sheet.png"
VERIFY = INBOX / "verify"

# Sheet rectangles (x0, y0, x1, y1) measured with a connected-component pass at alpha>128.
CORE = {
    "left_M": (275, 108, 388, 247),
    "right_M": (279, 302, 390, 437),
    "chevron": (1208, 673, 1266, 742),
}
GLOW_MARGIN = 28            # px of own glow to keep around the core (alpha ~4 at 18 px)
ATT_FULL, ATT_ZERO = 18, 28  # attenuation: 1.0 until 18 px from core, 0 at 28 px
LOW_ALPHA_FIX = 64          # below this alpha RGB is replaced with the glow colour

manifest: dict = {"source": str(SRC.relative_to(ROOT)).replace("\\", "/"), "outputs": {}}


def largest_component(mask: np.ndarray) -> np.ndarray:
    """Boolean mask of the largest 4-connected component (drops label fragments)."""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    best, best_n = 0, 0
    n = 0
    for y in range(h):
        for x in range(w):
            if mask[y, x] and lab[y, x] == 0:
                n += 1
                q = deque([(y, x)])
                lab[y, x] = n
                cnt = 0
                while q:
                    cy, cx = q.popleft()
                    cnt += 1
                    for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and lab[ny, nx] == 0:
                            lab[ny, nx] = n
                            q.append((ny, nx))
                if cnt > best_n:
                    best, best_n = n, cnt
    return lab == best


def distance_to(mask: np.ndarray) -> np.ndarray:
    """Euclidean distance (px) from every pixel to the nearest True pixel of mask."""
    h, w = mask.shape
    edge = mask & ~(
        np.roll(mask, 1, 0) & np.roll(mask, -1, 0) & np.roll(mask, 1, 1) & np.roll(mask, -1, 1))
    ey, ex = np.nonzero(edge)
    ys, xs = np.mgrid[0:h, 0:w]
    d = np.full((h, w), 1e9, np.float32)
    for i in range(0, len(ey), 64):
        by = ey[i:i + 64][:, None, None].astype(np.float32)
        bx = ex[i:i + 64][:, None, None].astype(np.float32)
        dd = np.sqrt((ys[None] - by) ** 2 + (xs[None] - bx) ** 2).min(axis=0)
        d = np.minimum(d, dd)
    d[mask] = 0
    return d


def smoothstep(e0: float, e1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def glow_colour(rgba: np.ndarray, dist: np.ndarray) -> np.ndarray:
    """Mean RGB of the glow band (alpha 40..120, 1..10 px from core), garbage colours excluded."""
    a = rgba[..., 3]
    rgb = rgba[..., :3].astype(np.float32)
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1), 0)
    band = (a >= 40) & (a <= 120) & (dist >= 1) & (dist <= 10) & (sat < 0.8)
    return rgb[band].mean(axis=0)


def clean_low_alpha(rgba: np.ndarray, colour: np.ndarray) -> np.ndarray:
    out = rgba.astype(np.float32)
    a = out[..., 3]
    t = np.clip(1.0 - a / LOW_ALPHA_FIX, 0, 1)[..., None]      # 1 at alpha 0 -> full replace
    out[..., :3] = out[..., :3] * (1 - t) + colour[None, None, :] * t
    return out


def cut_footprint(sheet: Image.Image, name: str, core_rect: tuple[int, int, int, int],
                  canvas: int, core_fill: float) -> Image.Image:
    x0, y0, x1, y1 = core_rect
    crop_rect = (x0 - GLOW_MARGIN, y0 - GLOW_MARGIN, x1 + GLOW_MARGIN, y1 + GLOW_MARGIN)
    crop = np.array(sheet.crop(crop_rect)).astype(np.float32)
    a = crop[..., 3]

    core = largest_component(a > 128)
    d = distance_to(core)
    att = 1.0 - smoothstep(ATT_FULL, ATT_ZERO, d)
    # Opaque or dark pixels away from the footprint = sheet label / neighbour fragments
    # (glow is always bright amber). Grown by 3 px so the label's antialiased rim goes too.
    dark = crop[..., :3].max(axis=-1) < 90
    frag = (~core) & ((a > 140) | ((a > 20) & dark & (d > 4)))
    for _ in range(3):
        frag |= np.roll(frag, 1, 0) | np.roll(frag, -1, 0) | np.roll(frag, 1, 1) | np.roll(frag, -1, 1)
    frag &= ~core
    att[frag] = 0.0
    a2 = a * att

    colour = glow_colour(crop, d)
    fixed = clean_low_alpha(np.dstack([crop[..., :3], a2]), colour)
    img = Image.fromarray(np.clip(fixed, 0, 255).astype(np.uint8), "RGBA")

    # Scale so the core's longer side fills core_fill of the canvas; centre the core bbox.
    ys, xs = np.nonzero(core)
    core_h, core_w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
    scale = canvas * core_fill / max(core_h, core_w)
    resized = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
    cx = (xs.min() + xs.max() + 1) / 2 * scale
    cy = (ys.min() + ys.max() + 1) / 2 * scale
    out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    out.alpha_composite(resized, (round(canvas / 2 - cx), round(canvas / 2 - cy)))

    manifest["outputs"][name] = {
        "sheet_core_rect": list(core_rect),
        "sheet_crop_rect": list(crop_rect),
        "core_px": [int(core_w), int(core_h)],
        "canvas": canvas,
        "scale": round(scale, 4),
        "core_fill_of_canvas": core_fill,
        "glow_colour_rgb": [round(float(c)) for c in colour],
        "attenuation_px": [ATT_FULL, ATT_ZERO],
        "fragments_removed_px": int(frag.sum()),
        "uv_up": "toes (row 0 = tip of the middle toe)" if "Footprint" in name
                 else "chevron points to row 0 (forward)",
    }
    return out


def radial_profile(sheet: Image.Image, cy: int, x_from: int, x_to: int) -> np.ndarray:
    """RGBA along a horizontal ray on row cy; index 0 = centre (x_from), growing radius."""
    arr = np.array(sheet).astype(np.float32)
    return arr[cy, x_from:x_to].copy()


def attenuate_tail(profile: np.ndarray, full_until: int, zero_at: int) -> np.ndarray:
    """Fade the profile's alpha to 0 between two radii (cuts merged neighbour haze)."""
    r = np.arange(len(profile), dtype=np.float32)
    profile[:, 3] *= 1.0 - smoothstep(full_until, zero_at, r)
    return profile


def regenerate_circle(profile: np.ndarray, canvas: int, radius_scale: float, name: str,
                      alpha_gain: float = 1.0, colour: np.ndarray | None = None) -> Image.Image:
    """Build a rotationally symmetric RGBA disc from a 1-D radial profile."""
    n = len(profile)
    ys, xs = np.mgrid[0:canvas, 0:canvas]
    r = np.sqrt((xs - (canvas - 1) / 2) ** 2 + (ys - (canvas - 1) / 2) ** 2) * (n / (canvas / 2 * radius_scale))
    outside = r >= n - 1
    r = np.clip(r, 0, n - 1.001)
    i0 = np.floor(r).astype(int)
    f = (r - i0)[..., None]
    p = profile[i0] * (1 - f) + profile[i0 + 1] * f
    p[outside, 3] = 0.0
    p[..., 3] = np.clip(p[..., 3] * alpha_gain, 0, 255)
    if colour is None:
        band = (profile[:, 3] >= 40) & (profile[:, 3] <= 200)
        colour = profile[band][:, :3].mean(axis=0)
    p = clean_low_alpha(p, colour)
    manifest["outputs"][name] = {
        "regenerated_from_radial_profile": True,
        "profile_len_px": int(n),
        "canvas": canvas,
        "radius_fill_of_canvas": radius_scale,
        "glow_colour_rgb": [round(float(c)) for c in colour],
    }
    return Image.fromarray(np.clip(p, 0, 255).astype(np.uint8), "RGBA")


def checker(size: tuple[int, int], cell: int = 16) -> Image.Image:
    ys, xs = np.mgrid[0:size[1], 0:size[0]]
    v = (((ys // cell) + (xs // cell)) % 2) * 60 + 120
    return Image.fromarray(np.dstack([v, v, v, np.full_like(v, 255)]).astype(np.uint8), "RGBA")


def verify(img: Image.Image, name: str) -> None:
    VERIFY.mkdir(parents=True, exist_ok=True)
    tiles = []
    for bg in (Image.new("RGBA", img.size, (205, 205, 205, 255)),
               Image.new("RGBA", img.size, (40, 40, 40, 255)),
               checker(img.size)):
        bg.alpha_composite(img)
        tiles.append(bg)
    out = Image.new("RGBA", (img.width * 3 + 20, img.height), (255, 0, 255, 255))
    for i, t in enumerate(tiles):
        out.paste(t, (i * (img.width + 10), 0))
    out.convert("RGB").save(VERIFY / f"{name}_verify.png")
    # Alpha-only view catches leftover fragments that hide on any background.
    Image.fromarray(np.array(img)[..., 3]).save(VERIFY / f"{name}_alpha.png")


def main() -> None:
    sheet = Image.open(SRC).convert("RGBA")
    assert sheet.size == (1448, 1086), sheet.size
    INBOX.mkdir(parents=True, exist_ok=True)

    outputs: dict[str, Image.Image] = {}
    outputs["T_Footprint_Left"] = cut_footprint(sheet, "T_Footprint_Left", CORE["left_M"], 512, 0.70)
    outputs["T_Footprint_Right"] = cut_footprint(sheet, "T_Footprint_Right", CORE["right_M"], 512, 0.70)
    outputs["T_NavForwardChevron"] = cut_footprint(sheet, "T_NavForwardChevron", CORE["chevron"], 128, 0.62)

    # 도착 링: ellipse centre (1089, 736), horizontal semi-axis 86 px (band peak ~r 82-86).
    # Sample the right half of the centre row (x 1089..1200). Section 04 shares one haze, so
    # the tail never reaches 0 on the sheet (floor ~39) — fade it out past the band ourselves.
    # The interior fill (alpha ~95) is halved so the real floor stays visible inside the ring.
    ring_prof = radial_profile(sheet, 736, 1089, 1201)
    ring_prof = attenuate_tail(ring_prof, 98, 111)
    ring_prof[:70, 3] *= 0.5
    outputs["T_NavArrivalRing"] = regenerate_circle(ring_prof, 512, 0.92, "T_NavArrivalRing")
    manifest["outputs"]["T_NavArrivalRing"].update({
        "sheet_profile": {"row_y": 736, "x_from": 1089, "x_to": 1201},
        "tail_fade_px": [98, 111], "interior_alpha_scale": 0.5, "band_peak_radius_px": 84})

    # 글로우 풀: ellipse centre (267, 953), horizontal semi-axis ~159 px. Left half of centre
    # row read outward (x 267 -> 100); the tail reaches alpha 1 on its own.
    pool_prof = radial_profile(sheet, 953, 100, 268)[::-1].copy()
    outputs["T_Footprint_Glow"] = regenerate_circle(pool_prof, 256, 1.0, "T_Footprint_Glow")
    manifest["outputs"]["T_Footprint_Glow"]["sheet_profile"] = {"row_y": 953, "x_from": 268, "x_to": 100}

    for name, img in outputs.items():
        img.save(INBOX / f"{name}.png")
        verify(img, name)
        a = np.array(img)[..., 3]
        manifest["outputs"][name].update({
            "alpha_min_max": [int(a.min()), int(a.max())],
            "opaque_frac": round(float((a >= 250).mean()), 4),
            "semi_frac": round(float(((a > 8) & (a < 250)).mean()), 4),
        })

    manifest["notes"] = [
        "Both footprints are real crops (left_M / right_M differ in detail); no UV mirroring.",
        "Sheet section 03 floor circles and section 02 sequences are references only — not exported.",
        "Ring and glow are regenerated as true circles from the sheet's horizontal profile "
        "(the sheet draws them as perspective ellipses with labels 6-7 px underneath).",
        "'도착' / '12 m' text is rendered by widgets, not baked into textures.",
    ]
    (INBOX / "crop_manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), "utf-8")
    print("EXTRACT_OK", ", ".join(outputs))


if __name__ == "__main__":
    main()
