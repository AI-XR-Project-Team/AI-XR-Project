"""조회 API 스키마 (Pydantic v2) — maps / markers / destinations.

앱 초기 로드용 정적 데이터. 좌표는 UE5 Z-up cm(Decimal)로 직렬화한다.
기존 `schemas/exhibit.py` 스타일(ConfigDict(from_attributes=True), Decimal) 준수.
"""
import uuid
from decimal import Decimal
from typing import Optional

from pydantic import BaseModel, ConfigDict


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
