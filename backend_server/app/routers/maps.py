"""조회 API 라우터 — 맵 메타 / 마커 / 목적지.

앱 초기 로드에 필요한 정적 데이터를 익명으로 제공한다(측위 정합용 마커 테이블,
목적지 메뉴). 경로 계산은 `POST /navigation/route`(navigation 라우터) 담당.
docs/nav-dev-plan.md 4단계, docs/navigation-server-design.md §5.1.
"""
import uuid
from typing import List, Optional

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.core.db import get_db
from app.models.map_space import MapSpace
from app.models.marker import Marker
from app.models.nav_node import NavNode
from app.schemas.maps import DestinationRead, MapRead, MarkerRead

router = APIRouter()


@router.get(
    "/maps/{map_id}",
    response_model=MapRead,
    summary="맵 메타 조회",
    responses={404: {"description": "map_id 없음"}},
)
def get_map(map_id: uuid.UUID, db: Session = Depends(get_db)) -> MapRead:
    """맵 1개의 메타(name, coord_system, origin_note). 없으면 404."""
    map_space = db.get(MapSpace, map_id)
    if map_space is None:
        raise HTTPException(status_code=404, detail="map not found")
    return map_space


@router.get(
    "/maps/{map_id}/markers",
    response_model=List[MarkerRead],
    summary="마커 목록 조회",
    responses={404: {"description": "map_id 없음"}},
)
def list_markers(map_id: uuid.UUID, db: Session = Depends(get_db)) -> List[Marker]:
    """이 맵의 QR 마커 목록(code→맵 좌표·heading). 앱이 측위 정합에 캐시한다."""
    if db.get(MapSpace, map_id) is None:
        raise HTTPException(status_code=404, detail="map not found")
    stmt = select(Marker).where(Marker.map_id == map_id).order_by(Marker.code)
    return list(db.scalars(stmt).all())


@router.get(
    "/maps/{map_id}/destinations",
    response_model=List[DestinationRead],
    summary="목적지 노드 목록 조회",
    responses={404: {"description": "map_id 없음"}},
)
def list_destinations(
    map_id: uuid.UUID,
    type: Optional[str] = Query(
        None, description="node_type 필터(예: exhibit). 생략 시 label 있는 노드 전체."
    ),
    db: Session = Depends(get_db),
) -> List[DestinationRead]:
    """안내 가능한 목적지 노드 목록. 기본은 label 이 있는 노드 전체이며 임의 노드도
    노출된다(전시물 한정 아님). `node_type` 은 앱 UI 분류 태그일 뿐 라우팅과 무관하고
    `?type=` 로 필터할 수 있다."""
    if db.get(MapSpace, map_id) is None:
        raise HTTPException(status_code=404, detail="map not found")

    stmt = select(NavNode).where(
        NavNode.map_id == map_id, NavNode.label.isnot(None)
    )
    if type is not None:
        stmt = stmt.where(NavNode.node_type == type)
    stmt = stmt.order_by(NavNode.label)

    return [
        DestinationRead(node_id=n.id, label=n.label, node_type=n.node_type)
        for n in db.scalars(stmt).all()
    ]
