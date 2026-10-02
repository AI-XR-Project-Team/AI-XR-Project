"""네비게이션 길찾기 라우터 — POST /navigation/route.

측위 비종속: 앱은 측위 결과를 맵 좌표(`from` pose)로만 넘긴다. 마커든 나중의
Meta VPS 든 이 계약은 불변이다(docs/nav-dev-plan.md §3).
"""
import uuid

import networkx as nx
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.map_space import MapSpace
from app.schemas.navigation import (
    RerouteRequest,
    RouteRequest,
    RouteResponse,
    Step,
    Waypoint,
)
from app.services import routing

router = APIRouter()

# 엣지 중간 투영점(내 위치·목적지 좌표)에 붙이는 합성 웨이포인트의 node_id.
# 실제 그래프 노드가 아님을 나타내는 nil UUID. 앱은 이 점을 좌표로만 쓴다(폴리라인
# 첫/끝 점 = 내 위치/목적지). node_id 로 목적지를 재조회하지 않는다.
_SYNTHETIC_NODE_ID = uuid.UUID(int=0)


def _foot_waypoint(foot) -> dict:
    """엣지 투영점(x,y,z) → 합성 웨이포인트 dict."""
    return {
        "node_id": _SYNTHETIC_NODE_ID,
        "pos_x_cm": foot[0],
        "pos_y_cm": foot[1],
        "pos_z_cm": foot[2],
        "node_type": "waypoint",
    }


def _compute_route(payload: RouteRequest, db: Session) -> RouteResponse:
    """route/reroute 공통 로직 — 엣지 투영 스냅 → 경로탐색 → 방향 안내(steps).

    `from` pose(그리고 좌표로 준 `to`)는 가장 가까운 '엣지'에 투영한다(spec §3.2).
    엣지 중간이면 양 끝점 각각에서 A* 를 돌려 (진입거리 + 경로비용) 이 작은 쪽을
    고른다 — 가까운 노드로만 스냅할 때 생기던 '엉뚱한 분기 선택'을 없앤다. 노드
    바로 위(진입거리 ~ 0)면 기존 노드 스냅과 동일하게 동작한다.
    `to` 를 `node_id` 로 주면 그 노드를 목적지로 고정(투영 안 함). `node_type` 은
    UI 태그일 뿐 라우팅과 무관. `accessible_only=true` 면 접근성 경로만.
    """
    if db.get(MapSpace, payload.map_id) is None:
        raise HTTPException(status_code=404, detail="map not found")

    graph = routing.get_graph(db, payload.map_id, accessible_only=payload.accessible_only)
    if graph.number_of_nodes() == 0:
        raise HTTPException(status_code=404, detail="no navigation graph for this map")

    start_cands = routing.snap_to_edge(
        graph, payload.from_.pos_x_cm, payload.from_.pos_y_cm, payload.from_.pos_z_cm
    )
    if not start_cands:
        raise HTTPException(status_code=404, detail="start node not found")

    if payload.to.node_id is not None:
        # 그래프는 이 맵의 노드만 담으므로, 없으면 잘못된/타맵 노드다.
        if payload.to.node_id not in graph:
            raise HTTPException(status_code=404, detail="destination node not found in this map")
        goal_cands = [(payload.to.node_id, 0.0, None)]
    else:
        goal_cands = routing.snap_to_edge(
            graph, payload.to.pos_x_cm, payload.to.pos_y_cm, payload.to.pos_z_cm
        )
        if not goal_cands:
            raise HTTPException(status_code=404, detail="destination node not found")

    # 진입 후보(출발 1~2 × 도착 1~2) 조합마다 A*. 총거리 = 출발 진입거리 +
    # 경로비용 + 도착 진입거리. 최소를 채택. 엣지 15개라 최대 4회 A* 는 무시할 비용.
    best = None  # (total, core_waypoints, start_foot, goal_foot)
    for s_node, s_dist, s_foot in start_cands:
        for g_node, g_dist, g_foot in goal_cands:
            try:
                result = routing.find_route(graph, s_node, g_node)
            except nx.NodeNotFound:
                raise HTTPException(status_code=404, detail="start or destination node not found")
            if result is None:
                continue
            core_wps, cost = result
            total = s_dist + cost + g_dist
            if best is None or total < best[0]:
                best = (total, core_wps, s_foot, g_foot)

    if best is None:
        raise HTTPException(status_code=422, detail="no path to destination")

    total_cm, core_wps, start_foot, goal_foot = best

    # 폴리라인 앞/뒤에 투영점을 끼워 "내 위치 → … → 목적지 좌표" 가 되게 한다.
    # 노드 위(foot=None)면 끼우지 않는다. 이래야 앱의 진행률 투영이 첫 세그먼트를
    # 내 근처에서 시작해 오탐(경로 이탈)이 나지 않는다.
    waypoints = []
    if start_foot is not None:
        waypoints.append(_foot_waypoint(start_foot))
    waypoints.extend(core_wps)
    if goal_foot is not None:
        waypoints.append(_foot_waypoint(goal_foot))

    steps = routing.build_steps(waypoints, initial_heading=payload.from_.heading_deg)
    return RouteResponse(
        total_distance_cm=round(total_cm, 2),
        waypoints=[Waypoint(**w) for w in waypoints],
        steps=[Step(**s) for s in steps],
    )


@router.post(
    "/navigation/route",
    response_model=RouteResponse,
    summary="최단 경로(웨이포인트+방향 안내) 계산",
    responses={
        404: {"description": "map_id 없음, 그래프 비어있음, 또는 목적지 노드가 이 맵에 없음"},
        422: {"description": "목적지까지 연결된 경로가 없음"},
    },
)
def route(payload: RouteRequest, db: Session = Depends(get_db)) -> RouteResponse:
    """현재 pose + 목적지 → 최단 경로 웨이포인트·총거리·방향 안내(steps)."""
    return _compute_route(payload, db)


@router.post(
    "/navigation/reroute",
    response_model=RouteResponse,
    summary="경로 이탈 시 재계산(route 와 동일 스키마)",
    responses={
        404: {"description": "map_id 없음, 그래프 비어있음, 또는 목적지 노드가 이 맵에 없음"},
        422: {"description": "목적지까지 연결된 경로가 없음"},
    },
)
def reroute(payload: RerouteRequest, db: Session = Depends(get_db)) -> RouteResponse:
    """경로를 벗어났을 때 새 pose 로 재계산. 로직·응답은 `/navigation/route` 와 동일.

    앱이 이탈을 감지해 현재 pose 를 `from` 으로 다시 호출하면 갱신된 경로/steps 를 준다.
    """
    return _compute_route(payload, db)
