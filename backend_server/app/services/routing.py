"""길찾기 코어 — nav_nodes/nav_edges → networkx 그래프 + 스냅 + 경로탐색.

측위 비종속: 입력은 맵 좌표(UE5 Z-up, cm)뿐이다. 임의 pose 를 그래프의
가장 가까운 노드에 스냅한 뒤 A*(휴리스틱=유클리드)로 최단 경로를 낸다.

방향 간선: MVP 는 데이터가 무방향(`bidirectional=true`) 중심이나, DiGraph 로
빌드해 `bidirectional=false`(계단 등 일방통행)도 정확히 처리한다 — true 면 양방향
간선 2개, false 면 from→to 단방향 1개만 추가한다.

그래프는 `map_id` 별 프로세스 메모리 캐시(수정 드묾). 시드 갱신 시 `invalidate()`
호출 또는 서버 재기동으로 대응한다(핫리로드는 후속 과제).
"""
import heapq
import math
import threading
import uuid
from itertools import count
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

    최단 '노드' 스냅. 사용자가 엣지 중간에 있으면 살짝 튈 수 있다. 엣지 투영
    스냅은 `snap_to_edge` 를 쓴다. 이 함수는 하위 호환·폴백용으로 남긴다.
    """
    best_id = None
    best_dist = math.inf
    for node_id, data in graph.nodes(data=True):
        d = _euclidean((x, y, z), (data["x"], data["y"], data["z"]))
        if d < best_dist:
            best_dist = d
            best_id = node_id
    return best_id


# 엣지 끝점 이 임계 거리(cm) 안이면 "그 노드 위에 서 있다"로 보고 노드 스냅으로
# 되돌린다. 측위 오차보다 작은 진입 거리는 무시해, 노드 근처(외길 맵 포함)에서
# 기존 노드 스냅과 동일한 결과를 보장한다(회귀 안전). 이보다 멀면 엣지 투영.
NODE_SNAP_EPS_CM = 50.0


def _project_to_segment(p, a, b):
    """점 p 를 선분 a-b 에 투영. 반환 (foot, du, dv, lateral).

    foot = 선분 위 가장 가까운 점(끝점으로 클램프), du/dv = foot 에서 a/b 까지의
    '선분 위' 거리(진입 거리), lateral = p 에서 선분까지의 수직 거리. 전부 3D cm.
    """
    ax, ay, az = a
    bx, by, bz = b
    px, py, pz = p
    abx, aby, abz = bx - ax, by - ay, bz - az
    ab2 = abx * abx + aby * aby + abz * abz
    if ab2 == 0.0:  # 길이 0 엣지(데이터 이상) — a 로 취급
        foot, t = a, 0.0
    else:
        t = ((px - ax) * abx + (py - ay) * aby + (pz - az) * abz) / ab2
        t = max(0.0, min(1.0, t))
        foot = (ax + t * abx, ay + t * aby, az + t * abz)
    seg_len = math.sqrt(ab2)
    du = t * seg_len
    dv = (1.0 - t) * seg_len
    lateral = _euclidean(p, foot)
    return foot, du, dv, lateral


def snap_to_edge(graph: nx.DiGraph, x: float, y: float, z: float):
    """(x,y,z) 를 가장 가까운 '엣지'에 투영해 A* 진입 후보를 낸다(spec §3.2).

    반환: [(node_id, entry_dist_cm, foot_or_None), ...]
      - 가장 가까운 엣지의 끝점까지가 NODE_SNAP_EPS_CM 이내면 그 노드로 스냅한
        단일 후보 [(node, 0.0, None)] — 노드 스냅과 동일(외길·회귀 안전).
      - 엣지 중간이면 양 끝점 두 후보 [(u, du, foot), (v, dv, foot)].
        호출측이 각 끝점에서 A* 를 돌려 (진입거리 + 경로비용) 이 작은 쪽을 고른다.
      - 엣지가 하나도 없으면 노드 스냅으로 폴백.
    foot=None 은 "노드 위" — 경로 폴리라인 앞에 투영점을 끼우지 않는다.
    """
    p = (x, y, z)
    best = None  # (lateral, u, v, foot, du, dv)
    for u, v in graph.edges():
        foot, du, dv, lateral = _project_to_segment(
            p, _node_pos(graph, u), _node_pos(graph, v)
        )
        if best is None or lateral < best[0]:
            best = (lateral, u, v, foot, du, dv)

    if best is None:  # 엣지 없음 — 노드 스냅 폴백
        nid = snap_to_graph(graph, x, y, z)
        return [(nid, 0.0, None)] if nid is not None else []

    _, u, v, foot, du, dv = best
    if du <= NODE_SNAP_EPS_CM:
        return [(u, 0.0, None)]
    if dv <= NODE_SNAP_EPS_CM:
        return [(v, 0.0, None)]
    return [(u, du, foot), (v, dv, foot)]


# 동점(같은 총거리) 경로가 여러 개일 때의 2차 정렬 기준. 값이 클수록 그 노드를
# '지나가는' 것을 꺼린다. 전시물/편의시설 한복판을 관통하는 대신 분기점·경유점
# (rank 0)을 지나는 경로를 택하게 한다. 예) E→G 가 E→F(전시물)→G 와
# E→H(분기점)→G 로 거리가 완전히 같을 때 H 쪽을 고른다.
# 이 값은 f(거리+휴리스틱)가 '동일'할 때만 순서를 가르므로 최단 경로 자체는
# 바뀌지 않는다(더 짧은 경로가 있으면 언제나 그쪽을 택한다).
_TRANSIT_RANK = {"exhibit": 2, "facility": 1}


def _transit_rank(node_type: Optional[str]) -> int:
    return _TRANSIT_RANK.get(node_type or "", 0)


def _astar_path(graph: nx.DiGraph, source, target, heuristic) -> List:
    """networkx.astar_path 의 tie-break 개량판.

    heap 우선순위에 (f, transit_rank) 를 써서, f(=거리+휴리스틱)가 같은 노드들
    사이에서는 `_transit_rank` 가 낮은(전시물/편의시설이 아닌) 노드를 먼저 확장한다.
    networkx 기본 구현은 삽입 순서(count)로만 tie-break 해서 간선 시드 순서에 따라
    전시물 관통 경로가 뽑히곤 했다. 거리 최적성은 그대로다.

    source/target 이 그래프에 없으면 nx.NodeNotFound, 경로 없으면 nx.NetworkXNoPath.
    """
    if source not in graph:
        raise nx.NodeNotFound(f"Source {source} is not in G")
    if target not in graph:
        raise nx.NodeNotFound(f"Target {target} is not in G")

    c = count()
    # (f, transit_rank, tie_counter, node, g_dist, parent)
    queue = [(0.0, 0, next(c), source, 0.0, None)]
    enqueued: Dict = {}   # node → (g_dist, h)
    explored: Dict = {}   # node → parent
    while queue:
        _, _, _, curnode, dist, parent = heapq.heappop(queue)
        if curnode == target:
            path = [curnode]
            node = parent
            while node is not None:
                path.append(node)
                node = explored[node]
            path.reverse()
            return path
        if curnode in explored:
            if explored[curnode] is None:
                continue
            qcost, _ = enqueued[curnode]
            if qcost < dist:
                continue
        explored[curnode] = parent
        for neighbor, attrs in graph[curnode].items():
            ncost = dist + attrs["weight"]
            if neighbor in enqueued:
                qcost, h = enqueued[neighbor]
                if qcost <= ncost:
                    continue
            else:
                h = heuristic(neighbor, target)
            enqueued[neighbor] = ncost, h
            # 목적지 노드는 '관통'이 아니라 도착이므로 rank 를 매기지 않는다.
            rank = 0 if neighbor == target else _transit_rank(
                graph.nodes[neighbor].get("node_type"))
            heapq.heappush(
                queue, (ncost + h, rank, next(c), neighbor, ncost, curnode))
    raise nx.NetworkXNoPath(f"Node {target} not reachable from {source}")


def find_route(graph: nx.DiGraph, start, goal) -> Optional[Tuple[List[dict], float]]:
    """start→goal 최단 경로. A*(유클리드 휴리스틱).

    반환: (웨이포인트 dict 리스트, 총거리 cm) 또는 경로가 없으면 None.
    각 웨이포인트: {node_id, pos_x_cm, pos_y_cm, pos_z_cm, node_type}.
    start/goal 이 그래프에 없으면 nx.NodeNotFound 를 그대로 올린다(호출측이 404 처리).

    거리가 완전히 같은 경로가 여럿이면 `_astar_path` 의 tie-break 로 전시물·편의시설
    관통을 피하는 쪽을 택한다(spec: E→G 동점 시 F 대신 H 경유).
    """
    def heuristic(a, b) -> float:
        return _euclidean(_node_pos(graph, a), _node_pos(graph, b))

    try:
        path = _astar_path(graph, start, goal, heuristic=heuristic)
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
