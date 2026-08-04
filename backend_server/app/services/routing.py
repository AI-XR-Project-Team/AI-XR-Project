"""길찾기 코어 — nav_nodes/nav_edges → networkx 그래프 + 스냅 + 경로탐색.

측위 비종속: 입력은 맵 좌표(UE5 Z-up, cm)뿐이다. 임의 pose 를 그래프의
가장 가까운 노드에 스냅한 뒤 A*(휴리스틱=유클리드)로 최단 경로를 낸다.

방향 간선: MVP 는 데이터가 무방향(`bidirectional=true`) 중심이나, DiGraph 로
빌드해 `bidirectional=false`(계단 등 일방통행)도 정확히 처리한다 — true 면 양방향
간선 2개, false 면 from→to 단방향 1개만 추가한다.

그래프는 `map_id` 별 프로세스 메모리 캐시(수정 드묾). 시드 갱신 시 `invalidate()`
호출 또는 서버 재기동으로 대응한다(핫리로드는 후속 과제).
"""
import math
import threading
import uuid
from typing import Dict, List, Optional, Tuple

import networkx as nx
from sqlalchemy.orm import Session

from app.models.nav_edge import NavEdge
from app.models.nav_node import NavNode

# (map_id, accessible_only) → 빌드된 그래프. 노드/간선에 ORM 객체가 아니라 순수
# 값만 담으므로 요청 세션이 닫혀도 detached-instance 문제가 없다. accessible_only
# 변형은 계단 등(accessible=false)을 뺀 별도 그래프라 캐시 키에 포함한다.
_graph_cache: Dict[Tuple[uuid.UUID, bool], nx.DiGraph] = {}
_cache_lock = threading.Lock()

# 방향 안내(steps) 판정 임계각(도). |Δheading| < STRAIGHT_MAX 직진, 그 이상은
# 부호로 좌/우회전. 실제 동선에서 튜닝 대상(spec §7).
STRAIGHT_MAX_DEG = 30.0


def _euclidean(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
    """두 좌표 간 유클리드 거리(cm)."""
    return math.dist(a, b)


def build_graph(db: Session, map_id: uuid.UUID,
                accessible_only: bool = False) -> nx.DiGraph:
    """해당 맵의 노드/간선을 networkx DiGraph 로 빌드한다.

    노드 속성: x, y, z(float, cm), node_type. 간선 속성: weight(distance_cm;
    NULL 이면 노드 좌표로 유클리드 계산). 캐시하지 않는 순수 빌더이므로
    테스트에서 직접 호출하거나 `get_graph` 를 통해 캐시할 수 있다.

    `accessible_only=True` 면 `accessible=false` 간선(계단 등)을 제외해
    휠체어/유아차 경로만 남긴다.
    """
    graph = nx.DiGraph()

    nodes = db.query(NavNode).filter_by(map_id=map_id).all()
    for node in nodes:
        graph.add_node(
            node.id,
            x=float(node.pos_x_cm),
            y=float(node.pos_y_cm),
            z=float(node.pos_z_cm),
            node_type=node.node_type,
        )

    edges = db.query(NavEdge).filter_by(map_id=map_id).all()
    for edge in edges:
        # 한쪽 노드라도 그래프에 없으면(데이터 정합 오류) 건너뛴다.
        if edge.from_node_id not in graph or edge.to_node_id not in graph:
            continue
        # 접근성 경로만 요청되면 계단 등(accessible=false) 간선은 뺀다.
        if accessible_only and not edge.accessible:
            continue
        if edge.distance_cm is not None:
            weight = float(edge.distance_cm)
        else:
            weight = _euclidean(_node_pos(graph, edge.from_node_id),
                                _node_pos(graph, edge.to_node_id))
        graph.add_edge(edge.from_node_id, edge.to_node_id, weight=weight)
        if edge.bidirectional:
            graph.add_edge(edge.to_node_id, edge.from_node_id, weight=weight)

    return graph


def get_graph(db: Session, map_id: uuid.UUID,
              accessible_only: bool = False) -> nx.DiGraph:
    """map_id 그래프를 캐시에서 반환(없으면 빌드해 캐시).

    일반 경로와 접근성 경로는 서로 다른 그래프라 `accessible_only` 별로 캐시한다.
    """
    key = (map_id, accessible_only)
    with _cache_lock:
        graph = _graph_cache.get(key)
        if graph is None:
            graph = build_graph(db, map_id, accessible_only=accessible_only)
            _graph_cache[key] = graph
        return graph


def invalidate(map_id: Optional[uuid.UUID] = None) -> None:
    """캐시 무효화. map_id 를 주면 해당 맵의 두 변형 모두, 없으면 전체."""
    with _cache_lock:
        if map_id is None:
            _graph_cache.clear()
        else:
            for accessible_only in (False, True):
                _graph_cache.pop((map_id, accessible_only), None)


def _node_pos(graph: nx.DiGraph, node_id) -> Tuple[float, float, float]:
    data = graph.nodes[node_id]
    return (data["x"], data["y"], data["z"])


def snap_to_graph(graph: nx.DiGraph, x: float, y: float, z: float):
    """(x,y,z) 에 가장 가까운 노드 id 를 반환. 빈 그래프면 None.

    MVP 는 최단 노드 스냅이다. 사용자가 엣지 중간에 있으면 살짝 튈 수 있으며,
    엣지 투영 스냅은 개선 과제(spec §7).
    """
    best_id = None
    best_dist = math.inf
    for node_id, data in graph.nodes(data=True):
        d = _euclidean((x, y, z), (data["x"], data["y"], data["z"]))
        if d < best_dist:
            best_dist = d
            best_id = node_id
    return best_id


def find_route(graph: nx.DiGraph, start, goal) -> Optional[Tuple[List[dict], float]]:
    """start→goal 최단 경로. A*(유클리드 휴리스틱).

    반환: (웨이포인트 dict 리스트, 총거리 cm) 또는 경로가 없으면 None.
    각 웨이포인트: {node_id, pos_x_cm, pos_y_cm, pos_z_cm, node_type}.
    start/goal 이 그래프에 없으면 nx.NodeNotFound 를 그대로 올린다(호출측이 404 처리).
    """
    def heuristic(a, b) -> float:
        return _euclidean(_node_pos(graph, a), _node_pos(graph, b))

    try:
        path = nx.astar_path(graph, start, goal, heuristic=heuristic, weight="weight")
    except nx.NetworkXNoPath:
        return None

    total_cm = 0.0
    for u, v in zip(path, path[1:]):
        total_cm += graph[u][v]["weight"]

    waypoints = []
    for node_id in path:
        data = graph.nodes[node_id]
        waypoints.append({
            "node_id": node_id,
            "pos_x_cm": data["x"],
            "pos_y_cm": data["y"],
            "pos_z_cm": data["z"],
            "node_type": data["node_type"],
        })
    return waypoints, round(total_cm, 2)


# --- 방향 안내(steps) 생성 ------------------------------------------------
#
# 웨이포인트 좌표만으로 사람이 읽는 안내문을 만든다. 진행 방향(heading)은 XY 평면
# 투영(z=높이 무시)에서 +X축 기준 CCW 도로 계산하고, 연속 세그먼트의 heading 변화
# (Δ)로 좌/우회전을 판정한다(spec §3). 회전 부호 규약: **Δ>0(CCW)=좌회전,
# Δ<0(CW)=우회전** — +Y(북)로 걷다 -X(서)를 향하면 좌회전. 실제 UE5 축·체감과
# 어긋나면 여기 한 곳(_turn_from_delta)만 뒤집으면 된다(spec §7, 팀 확인).
#
# 직진 거리는 웨이포인트 좌표 간 유클리드다. 간선 distance_cm 를 수동 지정한 경우
# 총거리(간선 가중치 합)와 미세하게 다를 수 있으나, 시드 기본값(None→좌표 계산)
# 에선 동일하다.

# 도착 안내 문안 — node_type(UI 태그)에 맞춘 표현. 미지정 타입은 기본 문구.
_ARRIVE_TEXT = {
    "exhibit": "전시물에 도착했습니다",
    "facility": "편의시설에 도착했습니다",
    "entrance": "출입구에 도착했습니다",
}


def _seg_heading(a: dict, b: dict) -> float:
    """웨이포인트 a→b 진행 방향(+X 기준 CCW 도). XY 평면 투영."""
    return math.degrees(math.atan2(b["pos_y_cm"] - a["pos_y_cm"],
                                   b["pos_x_cm"] - a["pos_x_cm"]))


def _norm_delta(deg: float) -> float:
    """각도차를 (-180, 180] 로 정규화."""
    d = (deg + 180.0) % 360.0 - 180.0
    return 180.0 if d == -180.0 else d


def _turn_from_delta(delta: float) -> Optional[str]:
    """Δheading → 'left'|'right'|None(직진). |Δ|<STRAIGHT_MAX 는 직진."""
    if abs(delta) < STRAIGHT_MAX_DEG:
        return None
    return "left" if delta > 0 else "right"


def _meters(cm: float) -> int:
    """cm → 사람이 읽는 미터(최소 1m 로 표기, 0m 방지)."""
    return max(1, round(cm / 100.0))


def _straight_step(run_cm: float) -> dict:
    return {
        "instruction": f"앞으로 {_meters(run_cm)}m 직진하세요",
        "distance_cm": round(run_cm, 2),
        "turn": "straight",
        "arrive": False,
    }


def _turn_step(turn: str) -> dict:
    text = "좌회전하세요" if turn == "left" else "우회전하세요"
    return {"instruction": text, "distance_cm": None, "turn": turn, "arrive": False}


def _arrive_step(waypoint: dict) -> dict:
    text = _ARRIVE_TEXT.get(waypoint["node_type"], "목적지에 도착했습니다")
    return {"instruction": text, "distance_cm": None, "turn": None, "arrive": True}


def build_steps(waypoints: List[dict],
                initial_heading: Optional[float] = None) -> List[dict]:
    """웨이포인트 리스트 → 방향 안내 steps(직진/좌우회전/도착).

    각 step: {instruction, distance_cm?, turn?('left'|'right'|'straight'), arrive}.
    `initial_heading`(현재 pose 의 heading_deg)을 주면 첫 세그먼트와의 각차로
    출발 회전 안내를 앞에 붙인다(어느 쪽으로 걷기 시작할지). 없으면 웨이포인트
    간 회전만 안내한다.
    """
    steps: List[dict] = []
    n = len(waypoints)
    if n == 0:
        return steps
    if n == 1:  # 출발=도착 (스냅 결과 동일 노드)
        steps.append(_arrive_step(waypoints[0]))
        return steps

    headings = [_seg_heading(waypoints[i], waypoints[i + 1]) for i in range(n - 1)]

    # 출발 회전: 현재 바라보는 방향 → 첫 세그먼트 방향.
    if initial_heading is not None:
        turn = _turn_from_delta(_norm_delta(headings[0] - initial_heading))
        if turn:
            steps.append(_turn_step(turn))

    run_cm = 0.0
    for i in range(n - 1):
        run_cm += _euclidean(
            (waypoints[i]["pos_x_cm"], waypoints[i]["pos_y_cm"], waypoints[i]["pos_z_cm"]),
            (waypoints[i + 1]["pos_x_cm"], waypoints[i + 1]["pos_y_cm"], waypoints[i + 1]["pos_z_cm"]),
        )
        # 웨이포인트 i+1 에서 다음 세그먼트로 꺾이는지 판정(마지막 세그먼트 제외).
        if i + 1 < n - 1:
            turn = _turn_from_delta(_norm_delta(headings[i + 1] - headings[i]))
            if turn:
                steps.append(_straight_step(run_cm))
                run_cm = 0.0
                steps.append(_turn_step(turn))

    if run_cm > 0:
        steps.append(_straight_step(run_cm))
    steps.append(_arrive_step(waypoints[-1]))
    return steps
