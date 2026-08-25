"""네비게이션 시드 로더 — seeds/nav/*.csv → DB (멱등).

사람이 채우는 맵 데이터(CSV)를 DB에 적재한다. CSV 의 임시 문자열 키
(`map_key`, `node_key`, 마커 `code`, `exhibit_label`)를 실제 UUID PK 로
변환·연결하고, `nav_edges.distance_cm` 이 비면 노드 좌표로 자동 계산한다.

실행 (backend_server/ 를 작업 디렉터리로):
    python scripts/seed_nav.py
    # 또는
    python -m scripts.seed_nav

재실행해도 자연키로 존재를 확인하므로 중복 삽입되지 않는다(멱등).
로드 순서(FK 의존): map_spaces → nav_nodes → nav_edges → markers → exhibits_nav.
좌표 규약: UE5 Z-up, 단위 cm.
"""
import csv
import json
import math
import os
import sys
from decimal import Decimal, InvalidOperation

# `python scripts/seed_nav.py` 로 직접 실행해도 app 패키지를 찾도록 루트를 경로에 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal          # noqa: E402
from app.models.dinosaur import Dinosaur      # noqa: E402
from app.models.exhibit import Exhibit        # noqa: E402
from app.models.map_space import MapSpace     # noqa: E402
from app.models.marker import Marker          # noqa: E402
from app.models.nav_edge import NavEdge       # noqa: E402
from app.models.nav_node import NavNode       # noqa: E402

# seeds/nav/ 는 이 스크립트(scripts/) 의 형제 디렉터리
SEED_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "seeds", "nav"
)


# --- CSV 유틸 --------------------------------------------------------

def _read_csv(name):
    """seeds/nav/<name> 을 dict 행 리스트로 읽는다(UTF-8, 한글 라벨)."""
    path = os.path.join(SEED_DIR, name)
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def _dec(value):
    """CSV 문자열 → Decimal. 빈 값이면 None."""
    if value is None or str(value).strip() == "":
        return None
    try:
        return Decimal(str(value).strip())
    except InvalidOperation:
        raise ValueError(f"숫자로 해석할 수 없는 값: {value!r}")


def _bool(value, default=True):
    """CSV 문자열 → bool ('true'/'false'/'1'/'0'). 빈 값이면 default."""
    s = str(value).strip().lower() if value is not None else ""
    if s == "":
        return default
    return s in ("true", "1", "yes", "y", "t")


def _load_outlines():
    """seeds/nav/map_outline.json → {map_key: outline_json 문자열}.

    파일 하나에 단일 맵(객체) 또는 여러 맵(리스트)을 담을 수 있다. 각 항목에서
    `outline`·`obstacles` 만 추출해 map_spaces.outline_json 에 저장할 JSON 문자열로
    직렬화한다. 파일이 없으면 빈 dict(구버전/외곽선 없는 맵 호환)."""
    path = os.path.join(SEED_DIR, "map_outline.json")
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    items = data if isinstance(data, list) else [data]
    outlines = {}
    for item in items:
        map_key = (item.get("map_key") or "").strip()
        if not map_key:
            continue
        payload = {
            "outline": item.get("outline", []),
            "obstacles": item.get("obstacles", []),
        }
        # ensure_ascii=False 로 사람이 읽는 값 유지, 키 순서 고정으로 재시드 시 동일 문자열.
        outlines[map_key] = json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return outlines


# --- 시드 로직 (멱등) -------------------------------------------------

def seed() -> None:
    db = SessionLocal()
    try:
        map_ids = _seed_map_spaces(db)
        node_ids = _seed_nav_nodes(db, map_ids)
        _seed_nav_edges(db, map_ids, node_ids)
        _seed_markers(db, map_ids)
        _seed_exhibits_nav(db, node_ids)

        db.commit()
        print("네비 시드 완료.")
    finally:
        db.close()


def _seed_map_spaces(db):
    """map_spaces 적재. name 을 자연키로 멱등. 반환: map_key → id.

    outline_json 은 seeds/nav/map_outline.json 에서 map_key 로 찾아 채운다. 이미
    존재하는 맵이라도 outline 이 비었거나 달라졌으면 갱신한다(외곽선만 추가하는
    재시드 지원)."""
    outlines = _load_outlines()
    map_ids = {}
    for row in _read_csv("map_spaces.csv"):
        key = row["map_key"].strip()
        name = row["name"].strip()
        outline_json = outlines.get(key)
        space = db.query(MapSpace).filter_by(name=name).first()
        if space is None:
            space = MapSpace(
                name=name,
                origin_note=(row.get("origin_note") or "").strip() or None,
                coord_system=(row.get("coord_system") or "").strip() or "ue5_zup_cm",
                outline_json=outline_json,
            )
            db.add(space)
            db.flush()  # space.id 확보
            print(f"[+] map_space 삽입: {name}"
                  f"{' (+outline)' if outline_json else ''}")
        else:
            if outline_json and space.outline_json != outline_json:
                space.outline_json = outline_json
                print(f"[~] map_space outline 갱신: {name}")
            else:
                print(f"[=] map_space 이미 존재: {name} (건너뜀)")
        map_ids[key] = space.id
    return map_ids


def _seed_nav_nodes(db, map_ids):
    """nav_nodes 적재. (map_id, 좌표) 를 자연키로 멱등. 반환: node_key → id.

    자연키를 좌표로 잡는 이유: node_key(n-g 등)는 DB에 없고, 노드의 물리적
    정체성은 '위치'다. label 은 UI 표시명이라 운영 중 바뀔 수 있는데(예: 4단계
    'G지점' → 7단계 '브라키오사우르스'), label 을 자연키로 쓰면 재시드가 같은
    좌표에 새 노드를 만들어 그래프가 두 겹으로 갈라진다(우회 경로 버그). 좌표로
    매칭하고 label/node_type 은 CSV 값으로 UPDATE 해 그 재발을 막는다.
    """
    node_ids = {}
    for row in _read_csv("nav_nodes.csv"):
        key = row["node_key"].strip()
        map_key = row["map_key"].strip()
        map_id = map_ids.get(map_key)
        if map_id is None:
            print(f"[!] nav_node 스킵: map_key '{map_key}' 미존재 (node {key})")
            continue

        x, y, z = _dec(row["pos_x_cm"]), _dec(row["pos_y_cm"]), _dec(row["pos_z_cm"])
        label = (row.get("label") or "").strip() or None
        node_type = (row.get("node_type") or "").strip() or "waypoint"

        # 자연키: (map_id, 좌표). 라벨이 바뀌어도 같은 노드로 인식된다.
        node = (
            db.query(NavNode)
            .filter_by(map_id=map_id, pos_x_cm=x, pos_y_cm=y, pos_z_cm=z)
            .first()
        )

        if node is None:
            node = NavNode(
                map_id=map_id, pos_x_cm=x, pos_y_cm=y, pos_z_cm=z,
                node_type=node_type, label=label,
            )
            db.add(node)
            db.flush()  # node.id 확보
            print(f"[+] nav_node 삽입: {key} ({label or f'{x},{y},{z}'})")
        elif node.label != label or node.node_type != node_type:
            old = node.label
            node.label, node.node_type = label, node_type
            print(f"[~] nav_node 갱신: {key} ({old} → {label})")
        else:
            print(f"[=] nav_node 이미 존재: {key} (건너뜀)")
        node_ids[key] = node.id
    return node_ids


def _seed_nav_edges(db, map_ids, node_ids):
    """nav_edges 적재. (map_id, from, to) 를 자연키로 멱등.
    distance_cm 빈 값은 노드 좌표로 유클리드 계산."""
    # distance 계산을 위해 노드 좌표 캐시 (id → (x,y,z))
    coords = {
        n.id: (n.pos_x_cm, n.pos_y_cm, n.pos_z_cm)
        for n in db.query(NavNode).all()
    }
    for row in _read_csv("nav_edges.csv"):
        map_key = row["map_key"].strip()
        from_key = row["from_node_key"].strip()
        to_key = row["to_node_key"].strip()
        map_id = map_ids.get(map_key)
        from_id = node_ids.get(from_key)
        to_id = node_ids.get(to_key)
        if map_id is None or from_id is None or to_id is None:
            print(f"[!] nav_edge 스킵: 미해석 키 "
                  f"(map={map_key}, from={from_key}, to={to_key})")
            continue

        exists = (
            db.query(NavEdge)
            .filter_by(map_id=map_id, from_node_id=from_id, to_node_id=to_id)
            .first()
        )
        if exists is not None:
            print(f"[=] nav_edge 이미 존재: {from_key}→{to_key} (건너뜀)")
            continue

        distance = _dec(row.get("distance_cm"))
        if distance is None:
            distance = _euclidean(coords[from_id], coords[to_id])
            print(f"    거리 자동계산: {from_key}→{to_key} = {distance} cm")

        db.add(NavEdge(
            map_id=map_id, from_node_id=from_id, to_node_id=to_id,
            distance_cm=distance,
            bidirectional=_bool(row.get("bidirectional")),
            accessible=_bool(row.get("accessible")),
        ))
        print(f"[+] nav_edge 삽입: {from_key}→{to_key}")


def _seed_markers(db, map_ids):
    """markers 적재. code(UNIQUE) 를 자연키로 멱등."""
    for row in _read_csv("markers.csv"):
        code = row["code"].strip()
        map_key = row["map_key"].strip()
        map_id = map_ids.get(map_key)
        if map_id is None:
            print(f"[!] marker 스킵: map_key '{map_key}' 미존재 (code {code})")
            continue

        exists = db.query(Marker).filter_by(code=code).first()
        if exists is not None:
            print(f"[=] marker 이미 존재: {code} (건너뜀)")
            continue

        db.add(Marker(
            map_id=map_id, code=code,
            marker_type=(row.get("marker_type") or "").strip() or "qr",
            pos_x_cm=_dec(row["pos_x_cm"]),
            pos_y_cm=_dec(row["pos_y_cm"]),
            pos_z_cm=_dec(row["pos_z_cm"]),
            heading_deg=_dec(row.get("heading_deg")) or Decimal("0"),
            note=(row.get("note") or "").strip() or None,
        ))
        print(f"[+] marker 삽입: {code}")


def _seed_exhibits_nav(db, node_ids):
    """exhibits_nav 적재. exhibit_label 로 전시물을 찾아 nav_node_id 설정.
    매칭 순서: exhibits.label 정확 일치 → dinosaurs.name_ko 폴백 → 스킵."""
    for row in _read_csv("exhibits_nav.csv"):
        label = row["exhibit_label"].strip()
        node_key = row["node_key"].strip()
        node_id = node_ids.get(node_key)
        if node_id is None:
            print(f"[!] exhibits_nav 스킵: node_key '{node_key}' 미존재 "
                  f"(label {label})")
            continue

        # (1) 전시물 라벨 정확 일치
        exhibit = db.query(Exhibit).filter_by(label=label).first()
        # (2) 폴백: 연결된 공룡 이름(name_ko) 일치
        if exhibit is None:
            exhibit = (
                db.query(Exhibit)
                .join(Dinosaur, Exhibit.dinosaur_id == Dinosaur.id)
                .filter(Dinosaur.name_ko == label)
                .first()
            )
        if exhibit is None:
            print(f"[!] exhibits_nav 스킵: '{label}' 에 매칭되는 전시물 없음 "
                  f"(exhibits.label / dinosaurs.name_ko)")
            continue

        if exhibit.nav_node_id == node_id:
            print(f"[=] exhibits_nav 이미 연결: {exhibit.label} → {node_key} (건너뜀)")
            continue

        exhibit.nav_node_id = node_id
        print(f"[+] exhibits_nav 연결: {exhibit.label} → {node_key}")


def _euclidean(a, b):
    """두 좌표(Decimal x,y,z) 간 유클리드 거리(cm)를 소수 2자리로 반환."""
    dx = float(a[0] - b[0])
    dy = float(a[1] - b[1])
    dz = float(a[2] - b[2])
    return Decimal(str(round(math.sqrt(dx * dx + dy * dy + dz * dz), 2)))


if __name__ == "__main__":
    seed()
