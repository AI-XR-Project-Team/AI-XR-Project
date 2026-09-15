#!/usr/bin/env python3
"""아르켈론 정보 화면 스킨 텍스처를 레퍼런스 시트에서 잘라 낸다.

일반 파이썬(PIL, numpy, cv2)이다. UE 파이썬이 아니다.

입력  docs/ai-handoff/inbox/archelon_info/src/ref_0N_*.png  (원본. 수정하지 않는다)
출력  docs/ai-handoff/inbox/archelon_info/*.png + manifest.json

원본 시트(1)(6)(7)는 알파 채널이 실제 컷아웃이다(알파 0 또는 ~252, 중간값은
가장자리뿐). 그래서 색상 키로 검정을 지우지 않고 알파를 그대로 쓴다. 알파 최댓값이
252 라 255 로 정규화만 한다.

시트에서 서로 겹친 요소(큰 거북 지느러미가 와이어프레임·렉시·발광 거북에 걸침,
지도가 골격 아래에 걸침)는 색온도로 가른다 — 거북은 갈색·금색(R > B), 나머지는
청록·흰색(B >= R). 지도의 "WESTERN INTERIOR SEAWAY" 글자는 cv2.inpaint 로 지운다.

아이콘은 흰색 마스크(알파 = 밝기)로 뽑는다. 앱이 청록/주황으로 색을 입힌다.
프로젝트의 기존 Material 아이콘과 같은 방식이다.

실행:  python tools/derive_archelon_info_assets.py
"""
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs/ai-handoff/inbox/archelon_info/src"
OUT = ROOT / "docs/ai-handoff/inbox/archelon_info"

manifest = {}


def load(name):
    return np.array(Image.open(SRC / name).convert("RGBA"))


def normalize_alpha(a):
    """시트 알파 최댓값이 252 라 살짝 비친다. 255 로 늘린다."""
    a = a.astype(np.float32)
    a[..., 3] = np.clip(a[..., 3] * (255.0 / 252.0), 0, 255)
    return a.astype(np.uint8)


def trim(a, pad=4, thresh=8):
    ys, xs = np.where(a[..., 3] > thresh)
    if len(xs) == 0:
        return a, (0, 0)
    x0, x1 = max(0, xs.min() - pad), min(a.shape[1], xs.max() + 1 + pad)
    y0, y1 = max(0, ys.min() - pad), min(a.shape[0], ys.max() + 1 + pad)
    return a[y0:y1, x0:x1], (int(x0), int(y0))


def save(name, arr, src_name, box, note):
    Image.fromarray(arr, "RGBA").save(OUT / name)
    manifest[name] = {"source": src_name, "box_xyxy": [int(v) for v in box],
                      "size": [int(arr.shape[1]), int(arr.shape[0])], "note": note}
    print(f"  {name:32s} {arr.shape[1]}x{arr.shape[0]}  <- {src_name} {box}")


def crop(sheet, box):
    x0, y0, x1, y1 = box
    return sheet[y0:y1, x0:x1].copy()


def drop_warm(a, margin=22):
    """갈색·금색(큰 거북) 픽셀을 지운다. 청록·흰색 요소만 남긴다."""
    r, b = a[..., 0].astype(int), a[..., 2].astype(int)
    warm = r > b + margin
    a = a.copy()
    a[warm, 3] = 0
    return a


def keep_cyan(a, min_br=60):
    """청록 요소만 남긴다. 큰 거북의 어두운 비늘은 B-R 이 40 이하, 청록 요소는 80 이상이다."""
    r, b = a[..., 0].astype(int), a[..., 2].astype(int)
    a = a.copy()
    a[(b - r) < min_br, 3] = 0
    return a


def keep_largest(a, thresh=60, keep_ratio=0.02):
    """알파 연결 성분 중 가장 큰 것(과 그 2% 이상 크기)만 남긴다. 떨어져 있는 이웃 요소 조각 제거."""
    m = (a[..., 3] > thresh).astype(np.uint8)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(m)
    if n <= 2:
        return a
    areas = stats[1:, cv2.CC_STAT_AREA]
    biggest = areas.max()
    keep = np.zeros(n, bool)
    keep[0] = False
    for i, ar in enumerate(areas, start=1):
        keep[i] = ar >= biggest * keep_ratio
    a = a.copy()
    a[~keep[lab], 3] = 0
    return a


def drop_cool(a, margin=10):
    """청록(지도) 픽셀을 지운다. 골격(베이지)만 남긴다."""
    r, b = a[..., 0].astype(int), a[..., 2].astype(int)
    cool = b > r + margin
    a = a.copy()
    a[cool, 3] = 0
    return a


def feather(a, radius=1.2):
    """마스크로 잘라 낸 가장자리가 계단지지 않게 알파만 살짝 흐린다."""
    al = Image.fromarray(a[..., 3]).filter(ImageFilter.GaussianBlur(radius))
    a = a.copy()
    a[..., 3] = np.minimum(a[..., 3], np.array(al))
    return a


def glyph_mask(a, inset, y_max=None, bright=150, canvas=128, pad=10):
    """아이콘 스프라이트에서 밝은 획만 흰색 마스크로 뽑아 정사각 캔버스에 가운데 놓는다."""
    h, w = a.shape[:2]
    lum = a[..., :3].max(axis=2).astype(np.float32)
    m = (lum >= bright) & (a[..., 3] > 60)
    m[:inset, :] = False
    m[h - inset:, :] = False
    m[:, :inset] = False
    m[:, w - inset:] = False
    if y_max is not None:
        m[y_max:, :] = False
    ys, xs = np.where(m)
    if len(xs) == 0:
        raise RuntimeError("glyph mask empty")
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    # 알파 = 밝기(배경 어두운 남색 → 0, 획 → 1). 마스크 바깥은 0.
    alpha = np.clip((lum - bright * 0.55) / (255 - bright * 0.55), 0, 1)
    alpha[~m] = 0
    glyph = alpha[y0:y1, x0:x1]
    gw, gh = x1 - x0, y1 - y0
    scale = (canvas - 2 * pad) / max(gw, gh)
    nw, nh = max(1, int(round(gw * scale))), max(1, int(round(gh * scale)))
    g = cv2.resize(glyph, (nw, nh), interpolation=cv2.INTER_AREA)
    out = np.zeros((canvas, canvas, 4), np.uint8)
    out[..., :3] = 255
    ox, oy = (canvas - nw) // 2, (canvas - nh) // 2
    out[oy:oy + nh, ox:ox + nw, 3] = (g * 255).astype(np.uint8)
    return out, (int(x0), int(y0), int(x1), int(y1))


def circle_inset_mask(a, inset):
    """원형 스프라이트의 링 테두리를 마스크에서 제외한다. 중심·반지름은 알파 bbox 로 잡는다."""
    h, w = a.shape[:2]
    ys, xs = np.where(a[..., 3] > 60)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    cy, cx = (y0 + y1) / 2, (x0 + x1) / 2
    yy, xx = np.mgrid[:h, :w]
    r = min(x1 - x0, y1 - y0) / 2 - inset
    return ((yy - cy) ** 2 + (xx - cx) ** 2) <= r * r


def circle_glyph(a, inset, y_max=None, bright=150):
    a = a.copy()
    a[~circle_inset_mask(a, inset), 3] = 0
    return glyph_mask(a, 0, y_max=y_max, bright=bright)


# ----------------------------------------------------------------------------
OUT.mkdir(parents=True, exist_ok=True)
print("[derive] hero")
hero = normalize_alpha(load("ref_01_hero.png"))
hero_t, off = trim(hero, pad=6)
save("T_ArchelonHero.png", hero_t, "ref_01_hero.png",
     (off[0], off[1], off[0] + hero_t.shape[1], off[1] + hero_t.shape[0]),
     "대표 이미지. 알파 컷아웃 그대로, 여백만 잘라 냄")

print("[derive] parts sheet")
p7 = normalize_alpha(load("ref_07_parts_sheet.png"))
p6 = normalize_alpha(load("ref_06_ui_sheet.png"))

# 소개 삽화: 발광 거북. 왼쪽 가장자리에 큰 거북 지느러미가 걸친다.
box = (732, 45, 1026, 285)
a = feather(keep_largest(keep_cyan(crop(p7, box))))
a, off = trim(a)
save("T_IntroIllustration.png", a, "ref_07_parts_sheet.png", box, "청록 발광 거북 실루엣. 갈색(큰 거북) 픽셀 제거")

# 특징 삽화: 와이어프레임 거북. 왼쪽에 큰 거북 뒷지느러미가 걸친다.
box = (800, 290, 1092, 660)
a = feather(keep_largest(keep_cyan(crop(p7, box))))
a, off = trim(a)
save("T_FeatureIllustration.png", a, "ref_07_parts_sheet.png", box, "청록 와이어프레임 거북. 갈색 픽셀 제거")

# 서식 삽화: 지도. 왼쪽 아래 'WESTERN INTERIOR SEAWAY' 글자를 inpaint 로 지운다.
box = (1095, 368, 1421, 681)
m = keep_largest(keep_cyan(crop(p7, box), min_br=50))
lum = m[..., :3].max(axis=2)
text = np.zeros(lum.shape, np.uint8)
tx0, ty0, tx1, ty1 = 45, 180, 185, 270          # 글자가 있는 영역(크롭 좌표)
region = lum[ty0:ty1, tx0:tx1]
text[ty0:ty1, tx0:tx1] = ((region > 120) & (m[ty0:ty1, tx0:tx1, 3] > 60)).astype(np.uint8) * 255
text = cv2.dilate(text, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))
rgb = cv2.inpaint(np.ascontiguousarray(m[..., :3]), text, 6, cv2.INPAINT_TELEA)
m[..., :3] = rgb
m[232:, :42, 3] = 0                              # 왼쪽 아래 큰 거북 비늘 조각
m, off = trim(m)
save("T_HabitatMap.png", m, "ref_07_parts_sheet.png", box,
     "북아메리카 서부 내해 지도. 왼쪽 아래 영문 라벨을 inpaint 로 제거(라벨은 UMG 가 그림)")

# 발견 삽화: 골격. 왼쪽 아래에 지도 일부가 걸친다.
box = (1033, 62, 1436, 411)
a = feather(keep_largest(drop_cool(crop(p7, box), margin=6)))
a, off = trim(a)
save("T_DiscoverySkeleton.png", a, "ref_07_parts_sheet.png", box, "거북 골격. 청록(지도) 픽셀 제거")

# 렉시 CTA. 위쪽에 큰 거북 지느러미가 걸친다.
box = (60, 420, 320, 672)
a = feather(keep_largest(drop_warm(crop(p7, box), margin=12), keep_ratio=0.5))   # 위쪽 거북 조각은 렉시와 떨어진 별개 성분
a, off = trim(a)
save("T_RexyCTA.png", a, "ref_07_parts_sheet.png", box, "렉시(가리키는 자세). 갈색 픽셀 제거")

# 원형 버튼 스프라이트(원 + 발광 + 글리프). 그대로 쓴다.
for name, box, note in [
    ("T_BtnBack.png", (348, 530, 463, 649), "뒤로 원형 버튼"),
    ("T_BtnMenu.png", (476, 530, 590, 649), "메뉴 원형 버튼"),
    ("T_BtnNext.png", (597, 520, 729, 656), "다음 원형 버튼(발광 강함)"),
]:
    a, off = trim(crop(p7, box))
    save(name, a, "ref_07_parts_sheet.png", box, note)

# 탭 아이콘(흰 마스크). 사각 스프라이트 위쪽 글리프만, 아래 라벨은 제외.
for name, box in [
    ("T_IcoTabIntro.png", (48, 672, 210, 824)),
    ("T_IcoTabFeature.png", (216, 672, 368, 824)),
    ("T_IcoTabHabitat.png", (372, 670, 539, 826)),
    ("T_IcoTabDiscovery.png", (548, 671, 711, 826)),
]:
    a = crop(p7, box)
    g, gb = glyph_mask(a, inset=22, y_max=100, bright=150)
    save(name, g, "ref_07_parts_sheet.png", box, f"탭 아이콘 흰 마스크. 글리프 bbox(크롭 내) {gb}")

# 소개 탭 요약 아이콘(주황 스프라이트 → 흰 마스크).
for name, box in [
    ("T_IcoStatLength.png", (758, 677, 905, 822)),
    ("T_IcoStatWeight.png", (924, 677, 1070, 822)),
    ("T_IcoStatDiet.png", (1088, 677, 1236, 823)),
    ("T_IcoStatEra.png", (1254, 676, 1400, 822)),
]:
    a = crop(p7, box)
    g, gb = circle_glyph(a, inset=16, y_max=95, bright=140)
    save(name, g, "ref_07_parts_sheet.png", box, f"요약 아이콘 흰 마스크. 글리프 bbox {gb}")

# 작은 아이콘 13개(청록 선 스프라이트 → 흰 마스크). 라벨은 원 밖이라 원 안 전체가 글리프.
small = [
    ("T_IcoFlipper.png", (39, 844, 138, 969)), ("T_IcoShell.png", (141, 844, 238, 969)),
    ("T_IcoBeak.png", (240, 844, 340, 970)), ("T_IcoWaves.png", (341, 844, 438, 970)),
    ("T_IcoPin.png", (440, 845, 535, 968)), ("T_IcoWaves2.png", (538, 844, 634, 968)),
    ("T_IcoThermo.png", (637, 844, 734, 967)), ("T_IcoJelly.png", (737, 843, 839, 967)),
    ("T_IcoRocks.png", (845, 843, 945, 968)), ("T_IcoLayers.png", (948, 843, 1047, 969)),
    ("T_IcoStones.png", (1051, 842, 1153, 969)), ("T_IcoDoc.png", (1166, 841, 1274, 947)),
    ("T_IcoCap.png", (1288, 843, 1404, 973)),
]
for name, box in small:
    a = crop(p7, box)
    # 원 아래쪽 라벨이 크롭에 걸릴 수 있어 원 지름(폭)까지만 본다.
    d = box[2] - box[0]
    a = a[:d + 4]
    g, gb = circle_glyph(a, inset=17, bright=150)
    save(name, g, "ref_07_parts_sheet.png", box, f"작은 아이콘 흰 마스크. 글리프 bbox {gb}")

# 배지·헤더 아이콘(ref_06). 글자는 버리고 왼쪽 아이콘만 마스크로.
for name, box, note in [
    ("T_IcoZonePin.png", (392, 30, 446, 100), "전시존 배지의 핀"),
    ("T_IcoMarine.png", (1190, 178, 1240, 230), "해양 파충류 배지의 물결"),
    ("T_IcoEra.png", (1190, 258, 1240, 310), "백악기 후기 배지의 산"),
]:
    a = crop(p6, box)
    g, gb = glyph_mask(a, inset=2, bright=120)
    save(name, g, "ref_06_ui_sheet.png", box, f"{note}. 흰 마스크, 글리프 bbox {gb}")

# 패널(9-slice). 발광 테두리를 살리려고 여백 6px 포함.
box = (21, 113, 771, 482)
a, off = trim(crop(p6, box), pad=0, thresh=4)
save("T_PanelGlow.png", a, "ref_06_ui_sheet.png", box, "남색 유리 패널 + 청록 발광 테두리. UMG Box(9-slice) 브러시용, 모서리 여백 약 40px")

# 발광 선. 선택 탭 밑줄과 패널 상단 액센트.
box = (790, 836, 1390, 868)
a, off = trim(crop(p6, box), pad=2)
save("T_GlowLine.png", a, "ref_06_ui_sheet.png", box, "청록 발광 가로선. 탭 밑줄")

# ----------------------------------------------------------------------------
print("[derive] background (procedural)")
W, H = 1080, 2400
yy, xx = np.mgrid[:H, :W].astype(np.float32)
t = yy / H
top = np.array([10, 34, 60], np.float32)      # 위: 조명이 닿는 남색
mid = np.array([6, 21, 35], np.float32)       # 가운데: #061523
bot = np.array([3, 9, 16], np.float32)        # 아래: 거의 검정
bg = np.where(t[..., None] < 0.45,
              top + (mid - top) * (t[..., None] / 0.45),
              mid + (bot - mid) * ((t[..., None] - 0.45) / 0.55))
# 수면 빛: 위 가운데 큰 라디얼 글로우
def glow(cx, cy, rx, ry, color, strength):
    d = ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2
    return np.exp(-d)[..., None] * np.array(color, np.float32) * strength
bg += glow(540, -200, 900, 700, (30, 110, 170), 0.45)
bg += glow(160, 300, 420, 380, (20, 80, 130), 0.35)
bg += glow(900, 420, 380, 300, (18, 70, 120), 0.3)
# 옅은 사광(빛줄기)
rays = np.zeros((H, W), np.float32)
for (x0, wdt, amp) in [(240, 90, 0.9), (420, 60, 0.6), (700, 110, 0.7), (880, 70, 0.5)]:
    dx = (xx - (x0 + yy * 0.22))
    rays += amp * np.exp(-(dx / wdt) ** 2) * np.clip(1 - yy / 1500, 0, 1)
bg += rays[..., None] * np.array((8, 26, 44), np.float32)
# 보케(작은 빛점)
rng = np.random.default_rng(7)
for _ in range(70):
    cx, cy = rng.uniform(0, W), rng.uniform(0, H * 0.75)
    r = rng.uniform(3, 14)
    s = rng.uniform(0.15, 0.7) * (1 - cy / H)
    bg += glow(cx, cy, r, r, (120, 200, 255), s)
# 미세 노이즈로 밴딩 방지
bg += rng.normal(0, 1.2, (H, W, 1)).astype(np.float32)
bg = np.clip(bg, 0, 255).astype(np.uint8)
bga = np.dstack([bg, np.full((H, W), 255, np.uint8)])
Image.fromarray(bga, "RGBA").save(OUT / "T_OceanBackground.png")
manifest["T_OceanBackground.png"] = {"source": "(procedural)", "box_xyxy": [0, 0, W, H], "size": [W, H],
                                     "note": "남색 해양 그라데이션 + 수면 글로우 + 빛줄기 + 보케. 전용 배경 원본이 없어 합성"}
print(f"  T_OceanBackground.png            {W}x{H}  (procedural)")

with open(OUT / "manifest.json", "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
print(f"[derive] {len(manifest)} files -> {OUT}")
