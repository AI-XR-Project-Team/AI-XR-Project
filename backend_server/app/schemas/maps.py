"""조회 API 스키마 (Pydantic v2) — maps / markers / destinations.

앱 초기 로드용 정적 데이터. 좌표는 UE5 Z-up cm(Decimal)로 직렬화한다.
기존 `schemas/exhibit.py` 스타일(ConfigDict(from_attributes=True), Decimal) 준수.
"""
import uuid
from decimal import Decimal
from typing import List, Optional, Tuple

from pydantic import BaseModel, ConfigDict, Field


class MapRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    name: str
    coord_system: str
    origin_note: Optional[str] = None


class MarkerRead(BaseModel):
    """QR 마커 — 앱이 code 로 조회해 맵 좌표·heading 을 얻어 측위를 정합한다."""

    model_config = ConfigDict(from_attributes=True)

    code: str
    marker_type: str
    pos_x_cm: Decimal  # UE5 Z-up, cm
    pos_y_cm: Decimal
    pos_z_cm: Decimal
    heading_deg: Decimal  # +X축 기준 CCW °
    note: Optional[str] = None


class DestinationRead(BaseModel):
    """안내 가능한 목적지 노드. node_type 은 UI 분류 태그일 뿐 라우팅과 무관."""

    model_config = ConfigDict(from_attributes=True)

    node_id: uuid.UUID
    label: Optional[str] = None
    node_type: str


# --- 전체 지도(GET /maps/{id}/graph) — 전체 미니맵을 한 번에 그리는 데이터 ---


class GraphNode(BaseModel):
    """그래프 노드 1개. 앱은 좌표를 월드로 변환해 전체 지도에 링으로 찍는다."""

    node_id: uuid.UUID
    pos_x_cm: float  # UE5 Z-up, cm
    pos_y_cm: float
    pos_z_cm: float
    node_type: str = Field(..., description="junction|waypoint|exhibit|entrance|facility")
    label: Optional[str] = None


class GraphEdge(BaseModel):
    """그래프 엣지 1개. 전체 엣지는 옅은 회색, 선택 경로는 강조로 그린다."""

    from_node_id: uuid.UUID
    to_node_id: uuid.UUID
    distance_cm: float  # NULL 이면 노드 좌표로 유클리드 계산해 채운다
    bidirectional: bool = True
    accessible: bool = True


class Obstacle(BaseModel):
    """내부 구조물(축 정렬 사각형). 전체 지도에 채워서 그린다. 단위 cm."""

    x0: float
    y0: float
    x1: float
    y1: float


class GraphRead(BaseModel):
    """전체 지도 한 번에: 노드·엣지·벽 외곽선·내부 구조물.

    여러 번 호출하지 않도록 한 응답에 담는다. outline/obstacles 는
    map_spaces.outline_json 에서 꺼내며, 없으면 빈 배열(구버전 맵 호환)."""

    map_id: uuid.UUID
    coord_system: str = "ue5_zup_cm"
    nodes: List[GraphNode] = Field(default_factory=list)
    edges: List[GraphEdge] = Field(default_factory=list)
    outline: List[Tuple[float, float]] = Field(
        default_factory=list, description="벽 폐곡선 꼭짓점 [x_cm, y_cm] 순서대로"
    )
    obstacles: List[Obstacle] = Field(default_factory=list)
