"""길찾기 코어 테스트 — 알려진 그래프에서 경로/거리 검증.

두 층위로 나눈다:
1. 서비스(routing.py) 순수 알고리즘 — DiGraph 픽스처로 DB 없이 스냅/경로 검증.
2. 라우터(POST /navigation/route) — nav_nodes/nav_edges/map_spaces 를 흉내내는
   스텁 세션으로 HTTP 계약(웨이포인트·상태코드) 검증. (conftest 의 도슨트 스텁과
   달리 여기선 query().filter_by().all() 까지 지원해야 하므로 로컬 스텁을 둔다.)

예시 그래프는 seeds/nav/*.csv(자연사관 floor1)를 그대로 반영한다:
    n-entrance(0,0,120) - j-lobby(0,600,120) - n-corridor-1(0,1400,120)
        j-lobby - wc-1(300,600,120)
        n-corridor-1 - ex-tyranno(-400,1400,120) / ex-triceratops(400,1400,120)
정문→티라노 기대: n-entrance→j-lobby→n-corridor-1→ex-tyranno, 600+800+400=1800cm.
"""
import uuid
from decimal import Decimal

import networkx as nx
import pytest
from fastapi.testclient import TestClient

from app.core.db import get_db
from app.main import app
from app.models.map_space import MapSpace
from app.models.nav_edge import NavEdge
from app.models.nav_node import NavNode
from app.services import routing

MAP_ID = uuid.UUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")
OTHER_MAP_ID = uuid.UUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb")

# 노드 id 를 사람이 읽는 키로 고정(단언을 읽기 쉽게).
NID = {
    "n-entrance": uuid.UUID("00000000-0000-0000-0000-000000000001"),
    "j-lobby": uuid.UUID("00000000-0000-0000-0000-000000000002"),
    "n-corridor-1": uuid.UUID("00000000-0000-0000-0000-000000000003"),
    "ex-tyranno": uuid.UUID("00000000-0000-0000-0000-000000000004"),
    "ex-triceratops": uuid.UUID("00000000-0000-0000-0000-000000000005"),
    "wc-1": uuid.UUID("00000000-0000-0000-0000-000000000006"),
    # 그래프에 있으나 어떤 간선과도 연결 안 된 고립 노드(도달 불가 테스트용).
    "isolated": uuid.UUID("00000000-0000-0000-0000-000000000009"),
}

_NODE_DEFS = [
    ("n-entrance", 0, 0, 120, "entrance", "정문"),
    ("j-lobby", 0, 600, 120, "junction", "로비 분기"),
    ("n-corridor-1", 0, 1400, 120, "waypoint", "중앙복도 중간"),
    ("ex-tyranno", -400, 1400, 120, "exhibit", "티라노사우루스 골격 앞"),
    ("ex-triceratops", 400, 1400, 120, "exhibit", "트리케라톱스 골격 앞"),
    ("wc-1", 300, 600, 120, "facility", "화장실"),
    ("isolated", 5000, 5000, 120, "waypoint", "고립 노드"),
]

# distance_cm 는 비워두고(None) 좌표로 자동 계산되게 한다(시드 로더와 동일 경로).
_EDGE_DEFS = [
    ("n-entrance", "j-lobby"),
    ("j-lobby", "wc-1"),
    ("j-lobby", "n-corridor-1"),
    ("n-corridor-1", "ex-tyranno"),
    ("n-corridor-1", "ex-triceratops"),
]


def _make_nodes():
    return [
        NavNode(
            id=NID[key], map_id=MAP_ID,
            pos_x_cm=Decimal(str(x)), pos_y_cm=Decimal(str(y)), pos_z_cm=Decimal(str(z)),
            node_type=ntype, label=label,
        )
        for key, x, y, z, ntype, label in _NODE_DEFS
    ]


def _make_edges():
    return [
        NavEdge(
            id=uuid.uuid4(), map_id=MAP_ID,
            from_node_id=NID[a], to_node_id=NID[b],
            distance_cm=None, bidirectional=True, accessible=True,
        )
        for a, b in _EDGE_DEFS
    ]


# --- 스텁 세션 (build_graph + 라우터가 쓰는 쿼리만 지원) -----------------

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
    """`get(MapSpace, id)` 와 `query(NavNode|NavEdge).filter_by(...).all()` 지원."""

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
    """각 테스트가 신선한 그래프를 빌드하도록 프로세스 캐시를 비운다."""
    routing.invalidate()
    yield
    routing.invalidate()


@pytest.fixture
def graph():
    """자연사관 floor1 그래프(스텁 DB 로 build_graph)."""
    db = StubSession([MapSpace(id=MAP_ID, name="자연사관 1층")],
                     _make_nodes(), _make_edges())
    return routing.build_graph(db, MAP_ID)


@pytest.fixture
def client():
    maps = [MapSpace(id=MAP_ID, name="자연사관 1층")]
    db = StubSession(maps, _make_nodes(), _make_edges())
    app.dependency_overrides[get_db] = lambda: db
    yield TestClient(app)
    app.dependency_overrides.clear()


# --- 1. 서비스 알고리즘 --------------------------------------------------

def test_build_graph_shape(graph):
    # 노드 7개, 양방향 간선 5개 → DiGraph 방향 간선 10개.
    assert graph.number_of_nodes() == 7
    assert graph.number_of_edges() == 10
    # NULL distance_cm 가 좌표로 계산됨: n-entrance→j-lobby = 600.
    assert graph[NID["n-entrance"]][NID["j-lobby"]]["weight"] == pytest.approx(600.0)


def test_route_entrance_to_tyranno(graph):
    waypoints, total = routing.find_route(graph, NID["n-entrance"], NID["ex-tyranno"])
    order = [w["node_id"] for w in waypoints]
    assert order == [NID["n-entrance"], NID["j-lobby"],
                     NID["n-corridor-1"], NID["ex-tyranno"]]
    assert total == pytest.approx(1800.0)  # 600 + 800 + 400
    assert waypoints[0]["node_type"] == "entrance"
    assert waypoints[-1]["node_type"] == "exhibit"


def test_snap_picks_nearest_node(graph):
    # 정문 근처(살짝 어긋난 pose) → n-entrance 로 스냅.
    assert routing.snap_to_graph(graph, 10, 20, 120) == NID["n-entrance"]
    # 티라노 전시물 앞 근처 → ex-tyranno.
    assert routing.snap_to_graph(graph, -390, 1390, 120) == NID["ex-tyranno"]


def test_bidirectional_reverse_path(graph):
    # 양방향이므로 티라노→정문도 가능(역순, 같은 거리).
    waypoints, total = routing.find_route(graph, NID["ex-tyranno"], NID["n-entrance"])
    assert [w["node_id"] for w in waypoints][0] == NID["ex-tyranno"]
    assert [w["node_id"] for w in waypoints][-1] == NID["n-entrance"]
    assert total == pytest.approx(1800.0)


def test_unreachable_returns_none(graph):
    # 고립 노드로는 경로가 없다.
    assert routing.find_route(graph, NID["n-entrance"], NID["isolated"]) is None


def test_missing_node_raises(graph):
    with pytest.raises(nx.NodeNotFound):
        routing.find_route(graph, NID["n-entrance"], uuid.uuid4())


# --- 2. 라우터 HTTP 계약 -------------------------------------------------

def _post(client, to):
    return client.post("/navigation/route", json={
        "map_id": str(MAP_ID),
        "from": {"pos_x_cm": 10, "pos_y_cm": 20, "pos_z_cm": 120, "heading_deg": 90},
        "to": to,
    })


def test_route_by_node_id(client):
    r = _post(client, {"node_id": str(NID["ex-tyranno"])})
    assert r.status_code == 200
    body = r.json()
    assert body["total_distance_cm"] == pytest.approx(1800.0)
    order = [w["node_id"] for w in body["waypoints"]]
    assert order == [str(NID["n-entrance"]), str(NID["j-lobby"]),
                     str(NID["n-corridor-1"]), str(NID["ex-tyranno"])]


def test_route_by_coords_snaps(client):
    # 티라노 전시물 앞 좌표를 주면 ex-tyranno 로 스냅되어 같은 경로.
    r = _post(client, {"pos_x_cm": -400, "pos_y_cm": 1400, "pos_z_cm": 120})
    assert r.status_code == 200
    assert r.json()["waypoints"][-1]["node_id"] == str(NID["ex-tyranno"])


def test_bad_map_id_404(client):
    r = client.post("/navigation/route", json={
        "map_id": str(OTHER_MAP_ID),
        "from": {"pos_x_cm": 0, "pos_y_cm": 0, "pos_z_cm": 120},
        "to": {"node_id": str(NID["ex-tyranno"])},
    })
    assert r.status_code == 404


def test_destination_node_not_in_map_404(client):
    r = _post(client, {"node_id": str(uuid.uuid4())})
    assert r.status_code == 404


def test_unreachable_destination_422(client):
    r = _post(client, {"node_id": str(NID["isolated"])})
    assert r.status_code == 422


def test_invalid_target_missing_coords_422(client):
    # node_id 도 없고 좌표도 불완전 → Pydantic 검증 실패(422).
    r = _post(client, {"pos_x_cm": 0, "pos_y_cm": 0})
    assert r.status_code == 422
