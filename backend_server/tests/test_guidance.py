"""방향 안내(steps)·리라우트·접근성 필터 테스트.

test_routing.py 와 같은 자연사관 floor1 스텁 그래프를 쓴다. 기하:
    n-entrance(0,0) → j-lobby(0,600) → n-corridor-1(0,1400): 모두 heading 90°(직진)
        n-corridor-1 → ex-tyranno(-400,1400): heading 180° → +90° 변화 = 좌회전
        n-corridor-1 → ex-triceratops(400,1400): heading 0° → -90° 변화 = 우회전
접근성 필터는 계단 간선(accessible=false)을 낀 별도 소형 그래프로 검증한다.
"""
import uuid
from decimal import Decimal

import pytest
from fastapi.testclient import TestClient

from app.core.db import get_db
from app.main import app
from app.models.map_space import MapSpace
from app.models.nav_edge import NavEdge
from app.models.nav_node import NavNode
from app.services import routing

MAP_ID = uuid.UUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")

NID = {
    "n-entrance": uuid.UUID("00000000-0000-0000-0000-000000000001"),
    "j-lobby": uuid.UUID("00000000-0000-0000-0000-000000000002"),
    "n-corridor-1": uuid.UUID("00000000-0000-0000-0000-000000000003"),
    "ex-tyranno": uuid.UUID("00000000-0000-0000-0000-000000000004"),
    "ex-triceratops": uuid.UUID("00000000-0000-0000-0000-000000000005"),
    "wc-1": uuid.UUID("00000000-0000-0000-0000-000000000006"),
}

_NODE_DEFS = [
    ("n-entrance", 0, 0, 120, "entrance", "정문"),
    ("j-lobby", 0, 600, 120, "junction", "로비 분기"),
    ("n-corridor-1", 0, 1400, 120, "waypoint", "중앙복도 중간"),
    ("ex-tyranno", -400, 1400, 120, "exhibit", "티라노사우루스 골격 앞"),
    ("ex-triceratops", 400, 1400, 120, "exhibit", "트리케라톱스 골격 앞"),
    ("wc-1", 300, 600, 120, "facility", "화장실"),
]

_EDGE_DEFS = [
    ("n-entrance", "j-lobby"),
    ("j-lobby", "wc-1"),
    ("j-lobby", "n-corridor-1"),
    ("n-corridor-1", "ex-tyranno"),
    ("n-corridor-1", "ex-triceratops"),
]


def _make_nodes(defs, nid, map_id=MAP_ID):
    return [
        NavNode(
            id=nid[key], map_id=map_id,
            pos_x_cm=Decimal(str(x)), pos_y_cm=Decimal(str(y)), pos_z_cm=Decimal(str(z)),
            node_type=ntype, label=label,
        )
        for key, x, y, z, ntype, label in defs
    ]


def _make_edges(defs, nid, map_id=MAP_ID):
    # defs: (a, b) 또는 (a, b, accessible). distance_cm 는 좌표로 자동 계산.
    edges = []
    for row in defs:
        a, b = row[0], row[1]
        accessible = row[2] if len(row) > 2 else True
        edges.append(NavEdge(
            id=uuid.uuid4(), map_id=map_id,
            from_node_id=nid[a], to_node_id=nid[b],
            distance_cm=None, bidirectional=True, accessible=accessible,
        ))
    return edges


# --- 스텁 세션 (test_routing.py 와 동일 계약) -----------------------------

class _StubQuery:
    def __init__(self, rows):
        self._rows = rows

    def filter_by(self, **kw):
        rows = [r for r in self._rows
                if all(getattr(r, k) == v for k, v in kw.items())]
        return _StubQuery(rows)

    def all(self):
        return list(self._rows)


class StubSession:
    def __init__(self, maps, nodes, edges):
        self._maps = {m.id: m for m in maps}
        self._by_model = {NavNode: nodes, NavEdge: edges}

    def get(self, model, pk):
        if model is MapSpace:
            return self._maps.get(pk)
        return None

    def query(self, model):
        return _StubQuery(self._by_model.get(model, []))


@pytest.fixture(autouse=True)
def _clear_graph_cache():
    routing.invalidate()
    yield
    routing.invalidate()


@pytest.fixture
def graph():
    db = StubSession([MapSpace(id=MAP_ID, name="자연사관 1층")],
                     _make_nodes(_NODE_DEFS, NID), _make_edges(_EDGE_DEFS, NID))
    return routing.build_graph(db, MAP_ID)


@pytest.fixture
def client():
    db = StubSession([MapSpace(id=MAP_ID, name="자연사관 1층")],
                     _make_nodes(_NODE_DEFS, NID), _make_edges(_EDGE_DEFS, NID))
    app.dependency_overrides[get_db] = lambda: db
    yield TestClient(app)
    app.dependency_overrides.clear()


# --- 1. steps 생성 (서비스) ----------------------------------------------

def _route_steps(graph, start, goal, initial_heading=None):
    waypoints, _ = routing.find_route(graph, start, goal)
    return routing.build_steps(waypoints, initial_heading=initial_heading)


def test_straight_path_is_go_straight_then_arrive(graph):
    # 정문→중앙복도: 전 구간 직진(90°). 직진 1개 + 도착.
    steps = _route_steps(graph, NID["n-entrance"], NID["n-corridor-1"])
    assert [s["turn"] for s in steps] == ["straight", None]
    assert steps[0]["distance_cm"] == pytest.approx(1400.0)  # 600 + 800
    assert steps[0]["instruction"] == "앞으로 14m 직진하세요"
    assert steps[-1]["arrive"] is True
    assert not any(s["arrive"] for s in steps[:-1])


def test_left_turn_to_tyranno(graph):
    # 정문→티라노: 복도에서 좌회전(북→서). 순서: 직진→좌회전→직진→도착.
    steps = _route_steps(graph, NID["n-entrance"], NID["ex-tyranno"])
    turns = [s["turn"] for s in steps]
    assert turns == ["straight", "left", "straight", None]
    left = next(s for s in steps if s["turn"] == "left")
    assert left["instruction"] == "좌회전하세요"
    assert steps[0]["distance_cm"] == pytest.approx(1400.0)   # 정문→복도
    assert steps[2]["distance_cm"] == pytest.approx(400.0)    # 복도→티라노
    assert steps[-1]["instruction"] == "전시물에 도착했습니다"


def test_right_turn_to_triceratops(graph):
    # 정문→트리케라톱스: 복도에서 우회전(북→동).
    steps = _route_steps(graph, NID["n-entrance"], NID["ex-triceratops"])
    assert [s["turn"] for s in steps] == ["straight", "right", "straight", None]
    assert steps[-1]["arrive"] is True


def test_initial_heading_prepends_start_turn(graph):
    # +X(동, heading 0°)를 보고 서 있다가 첫 세그먼트가 북(90°) → 출발 좌회전.
    steps = _route_steps(graph, NID["n-entrance"], NID["n-corridor-1"],
                         initial_heading=0.0)
    assert steps[0]["turn"] == "left"
    assert steps[1]["turn"] == "straight"
    # 이미 첫 세그먼트 방향(북, 90°)을 보고 있으면 출발 회전 없음.
    aligned = _route_steps(graph, NID["n-entrance"], NID["n-corridor-1"],
                           initial_heading=90.0)
    assert aligned[0]["turn"] == "straight"


def test_single_node_route_is_arrive_only():
    # 웨이포인트 1개(출발=도착) → 도착 step 만.
    steps = routing.build_steps([
        {"node_id": NID["wc-1"], "pos_x_cm": 300, "pos_y_cm": 600,
         "pos_z_cm": 120, "node_type": "facility"},
    ])
    assert len(steps) == 1
    assert steps[0]["arrive"] is True
    assert steps[0]["instruction"] == "편의시설에 도착했습니다"


# --- 2. HTTP 계약: route steps + reroute ---------------------------------

def _post(client, path, to, from_pose=None, accessible_only=False):
    body = {
        "map_id": str(MAP_ID),
        "from": from_pose or {"pos_x_cm": 10, "pos_y_cm": 20, "pos_z_cm": 120, "heading_deg": 90},
        "to": to,
    }
    if accessible_only:
        body["accessible_only"] = True
    return client.post(path, json=body)


def test_route_response_includes_steps(client):
    r = _post(client, "/navigation/route", {"node_id": str(NID["ex-tyranno"])})
    assert r.status_code == 200
    steps = r.json()["steps"]
    assert [s["turn"] for s in steps] == ["straight", "left", "straight", None]
    assert steps[-1]["arrive"] is True


def test_reroute_returns_updated_route(client):
    # 티라노로 가다 트리케라톱스 쪽(동)으로 이탈 → 리라우트하면 우회전 경로.
    off_path = {"pos_x_cm": 390, "pos_y_cm": 1390, "pos_z_cm": 120, "heading_deg": 90}
    r = _post(client, "/navigation/reroute", {"node_id": str(NID["ex-triceratops"])},
              from_pose=off_path)
    assert r.status_code == 200
    body = r.json()
    # 이미 트리케라톱스 노드에 스냅되므로 도착만 남는다(웨이포인트 1개).
    assert body["waypoints"][-1]["node_id"] == str(NID["ex-triceratops"])
    assert body["steps"][-1]["arrive"] is True


def test_reroute_shares_route_contract(client):
    # reroute 도 route 와 동일하게 404/422 계약을 지킨다.
    r = _post(client, "/navigation/reroute", {"node_id": str(uuid.uuid4())})
    assert r.status_code == 404


# --- 3. (선택) 접근성 필터 ------------------------------------------------

# 계단 지름길 vs 우회 램프. A→D 직통은 계단(accessible=false),
# A→B→C→D 는 접근 가능. accessible_only 면 계단을 피해 우회해야 한다.
_ACC_NID = {k: uuid.UUID(f"00000000-0000-0000-0000-00000000010{i}")
            for i, k in enumerate(["a", "b", "c", "d"], start=1)}
_ACC_NODES = [
    ("a", 0, 0, 120, "junction", "A"),
    ("b", 300, 0, 120, "waypoint", "B"),
    ("c", 300, 300, 120, "waypoint", "C"),
    ("d", 0, 300, 120, "exhibit", "D"),
]
_ACC_EDGES = [
    ("a", "d", False),   # 계단 지름길(300cm)
    ("a", "b", True),
    ("b", "c", True),
    ("c", "d", True),    # 우회 램프(900cm)
]


def _acc_graph(accessible_only):
    db = StubSession([MapSpace(id=MAP_ID, name="접근성 테스트")],
                     _make_nodes(_ACC_NODES, _ACC_NID),
                     _make_edges(_ACC_EDGES, _ACC_NID))
    return routing.build_graph(db, MAP_ID, accessible_only=accessible_only)


def test_accessible_only_avoids_stairs():
    default = _acc_graph(accessible_only=False)
    wp, total = routing.find_route(default, _ACC_NID["a"], _ACC_NID["d"])
    assert [w["node_id"] for w in wp] == [_ACC_NID["a"], _ACC_NID["d"]]  # 계단 직통
    assert total == pytest.approx(300.0)

    acc = _acc_graph(accessible_only=True)
    wp2, total2 = routing.find_route(acc, _ACC_NID["a"], _ACC_NID["d"])
    assert [w["node_id"] for w in wp2] == [
        _ACC_NID["a"], _ACC_NID["b"], _ACC_NID["c"], _ACC_NID["d"]]  # 램프 우회
    assert total2 == pytest.approx(900.0)
