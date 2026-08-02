"""네비게이션 길찾기 스키마 (Pydantic v2).

측위 비종속 계약: 서버는 **맵 좌표(UE5 Z-up, cm)** 만 입력받는다. 마커/VIO 든
나중의 Meta VPS 든 측위 결과를 `from` pose(맵 좌표)로 넘기기만 하면 된다.
좌표계·단위 규약은 `docs/navigation-server-design.md` §5 참조.
"""
import uuid
from typing import List, Optional

from pydantic import BaseModel, ConfigDict, Field, model_validator


class Pose(BaseModel):
    """현재 위치(측위 결과). heading 은 MVP 경로탐색에서는 쓰지 않고
    방향 안내(guidance 단계)를 위해 미리 받아둔다."""

    pos_x_cm: float = Field(..., description="맵 좌표 X (cm)")
    pos_y_cm: float = Field(..., description="맵 좌표 Y (cm)")
    pos_z_cm: float = Field(..., description="맵 좌표 Z (cm), UE5 Z-up")
    heading_deg: Optional[float] = Field(
        None, description="바라보는 방향(+X축 기준 CCW 도). MVP 경로탐색 미사용."
    )


class RouteTarget(BaseModel):
    """목적지 — `node_id`(임의 노드) 또는 임의 좌표 중 하나. 좌표면 가까운 노드로 스냅.
    `node_type` 은 UI 태그일 뿐 라우팅과 무관하므로 여기선 받지 않는다."""

    node_id: Optional[uuid.UUID] = Field(
        None, description="목적지 노드 id. 임의 좌표 대신 노드를 직접 지정할 때 사용."
    )
    pos_x_cm: Optional[float] = None
    pos_y_cm: Optional[float] = None
    pos_z_cm: Optional[float] = None

    @model_validator(mode="after")
    def _require_node_or_coords(self) -> "RouteTarget":
        if self.node_id is not None:
            return self
        coords = (self.pos_x_cm, self.pos_y_cm, self.pos_z_cm)
        if any(c is None for c in coords):
            raise ValueError("to 는 node_id 또는 (pos_x_cm, pos_y_cm, pos_z_cm) 전체가 필요합니다")
        return self


class RouteRequest(BaseModel):
    model_config = ConfigDict(
        populate_by_name=True,
        json_schema_extra={
            "example": {
                "map_id": "00000000-0000-0000-0000-000000000000",
                "from": {
                    "pos_x_cm": 0,
                    "pos_y_cm": 0,
                    "pos_z_cm": 120,
                    "heading_deg": 90,
                },
                "to": {"node_id": "11111111-1111-1111-1111-111111111111"},
            }
        },
    )

    map_id: uuid.UUID = Field(..., description="대상 맵(map_spaces.id).")
    # `from` 은 파이썬 예약어라 필드명은 from_, JSON 키는 alias 로 "from".
    from_: Pose = Field(..., alias="from", description="현재 위치 pose(맵 좌표).")
    to: RouteTarget = Field(..., description="목적지(node_id 또는 좌표).")


class Waypoint(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    node_id: uuid.UUID
    pos_x_cm: float
    pos_y_cm: float
    pos_z_cm: float
    node_type: str = Field(..., description="junction|waypoint|exhibit|entrance|facility (UI 태그)")


class RouteResponse(BaseModel):
    total_distance_cm: float = Field(..., description="경로 총 거리(cm).")
    waypoints: List[Waypoint] = Field(
        ..., description="출발 스냅 노드부터 목적지 노드까지 순서대로."
    )
