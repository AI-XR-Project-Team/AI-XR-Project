"""네비게이션 길찾기 라우터 — POST /navigation/route.

측위 비종속: 앱은 측위 결과를 맵 좌표(`from` pose)로만 넘긴다. 마커든 나중의
Meta VPS 든 이 계약은 불변이다(docs/nav-dev-plan.md §3).
"""
import networkx as nx
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.map_space import MapSpace
from app.schemas.navigation import RouteRequest, RouteResponse, Waypoint
from app.services import routing

router = APIRouter()


@router.post(
    "/navigation/route",
    response_model=RouteResponse,
    summary="최단 경로(웨이포인트) 계산",
    responses={
        404: {"description": "map_id 없음, 그래프 비어있음, 또는 목적지 노드가 이 맵에 없음"},
        422: {"description": "목적지까지 연결된 경로가 없음"},
    },
)
def route(payload: RouteRequest, db: Session = Depends(get_db)) -> RouteResponse:
    """현재 pose + 목적지 → 최단 경로 웨이포인트와 총거리.

    `from` pose 는 그래프의 가장 가까운 노드로 스냅한다. `to` 는 `node_id`(임의 노드)
    또는 임의 좌표(역시 스냅) 로 줄 수 있다. `node_type` 은 UI 태그일 뿐 라우팅과 무관.
    """
    if db.get(MapSpace, payload.map_id) is None:
        raise HTTPException(status_code=404, detail="map not found")

    graph = routing.get_graph(db, payload.map_id)
    if graph.number_of_nodes() == 0:
        raise HTTPException(status_code=404, detail="no navigation graph for this map")

    start = routing.snap_to_graph(
        graph, payload.from_.pos_x_cm, payload.from_.pos_y_cm, payload.from_.pos_z_cm
    )

    if payload.to.node_id is not None:
        # 그래프는 이 맵의 노드만 담으므로, 없으면 잘못된/타맵 노드다.
        if payload.to.node_id not in graph:
            raise HTTPException(status_code=404, detail="destination node not found in this map")
        goal = payload.to.node_id
    else:
        goal = routing.snap_to_graph(
            graph, payload.to.pos_x_cm, payload.to.pos_y_cm, payload.to.pos_z_cm
        )

    try:
        result = routing.find_route(graph, start, goal)
    except nx.NodeNotFound:
        raise HTTPException(status_code=404, detail="start or destination node not found")

    if result is None:
        raise HTTPException(status_code=422, detail="no path to destination")

    waypoints, total_cm = result
    return RouteResponse(
        total_distance_cm=total_cm,
        waypoints=[Waypoint(**w) for w in waypoints],
    )
