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

# map_id → 빌드된 그래프. 노드/간선에 ORM 객체가 아니라 순수 값만 담으므로
# 요청 세션이 닫혀도 detached-instance 문제가 없다.
_graph_cache: Dict[uuid.UUID, nx.DiGraph] = {}
_cache_lock = threading.Lock()


def _euclidean(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
    """두 좌표 간 유클리드 거리(cm)."""
    return math.dist(a, b)


def build_graph(db: Session, map_id: uuid.UUID) -> nx.DiGraph:
    """해당 맵의 노드/간선을 networkx DiGraph 로 빌드한다.

    노드 속성: x, y, z(float, cm), node_type. 간선 속성: weight(distance_cm;
    NULL 이면 노드 좌표로 유클리드 계산). 캐시하지 않는 순수 빌더이므로
    테스트에서 직접 호출하거나 `get_graph` 를 통해 캐시할 수 있다.
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
        if edge.distance_cm is not None:
            weight = float(edge.distance_cm)
        else:
            weight = _euclidean(_node_pos(graph, edge.from_node_id),
                                _node_pos(graph, edge.to_node_id))
        graph.add_edge(edge.from_node_id, edge.to_node_id, weight=weight)
        if edge.bidirectional:
            graph.add_edge(edge.to_node_id, edge.from_node_id, weight=weight)

    return graph


def get_graph(db: Session, map_id: uuid.UUID) -> nx.DiGraph:
    """map_id 그래프를 캐시에서 반환(없으면 빌드해 캐시)."""
    with _cache_lock:
        graph = _graph_cache.get(map_id)
        if graph is None:
            graph = build_graph(db, map_id)
            _graph_cache[map_id] = graph
        return graph


def invalidate(map_id: Optional[uuid.UUID] = None) -> None:
    """캐시 무효화. map_id 를 주면 해당 맵만, 없으면 전체."""
    with _cache_lock:
        if map_id is None:
            _graph_cache.clear()
        else:
            _graph_cache.pop(map_id, None)


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
