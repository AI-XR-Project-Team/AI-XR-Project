"""Cloud Anchor 스키마 (Pydantic v2) — 11단계 포인트 우선(D5') 모델.

11단계 D5': 좌표는 지도에서 사전 확정(포인트 행 먼저 생성)하고, 현장 등록은
그 포인트에 cloud_id 를 **바인딩**하는 작업이다. 그래서 스키마가 세 갈래다.

  1. CloudAnchorPointUpsert — 계획 시딩(좌표만). cloud_id 없음.
  2. CloudAnchorBind        — 현장 등록. cloud_id + 앱이 계산한 heading.
  3. CloudAnchorRead        — 방문객 앱이 받는 리졸브 목록.

heading 규약: +X축 기준 CCW °. 계획값은 전부 90°(D8, +Y 를 보고 설치)이고,
호스팅 순간 앱이 Δ 식으로 실측 heading 을 계산해 bind 로 보낸다.
schemas/maps.py MarkerRead 스타일 준수.
"""
from decimal import Decimal
from typing import Optional

from pydantic import BaseModel, ConfigDict, Field


class CloudAnchorRead(BaseModel):
    """방문객/관리자 앱이 받는 앵커 1건. cloud_id 는 미바인딩 포인트면 null."""

    model_config = ConfigDict(from_attributes=True)

    point_no: int
    edge: Optional[str] = None
    cloud_id: Optional[str] = None
    pos_x_cm: Decimal  # UE5 Z-up, cm
    pos_y_cm: Decimal
    pos_z_cm: Decimal
    heading_deg: Decimal  # +X축 기준 CCW °
    label: Optional[str] = None
    is_bound: bool = False
    is_verified: bool = False


class CloudAnchorPointUpsert(BaseModel):
    """지도 계획 시딩 — 좌표·edge·라벨만. cloud_id 는 받지 않는다(바인딩 전).

    입력 원본은 docs/nav-stage11/cloud-anchor-plan.csv.
    """

    pos_x_cm: Decimal
    pos_y_cm: Decimal
    pos_z_cm: Decimal = Decimal("0")      # D5'b — 앵커는 무조건 바닥
    heading_deg: Decimal = Decimal("90")  # D8 — 계획값은 전부 +Y
    edge: Optional[str] = Field(default=None, max_length=20)
    label: Optional[str] = Field(default=None, max_length=200)
    note: Optional[str] = Field(default=None, max_length=200)
    enabled: bool = True


class CloudAnchorBind(BaseModel):
    """현장 등록 — 호스팅 성공 직후 관리자 앱이 보낸다.

    heading_deg 는 앱이 호스팅 순간에 계산한 실측값이다(Δ 식):
        Δ = 90°(계획 방위) − 카메라 세션 yaw
        heading = 앵커 세션 yaw + Δ
    생략하면 계획값(90°)을 유지한다.
    """

    cloud_id: str = Field(..., min_length=1, max_length=200)
    heading_deg: Optional[Decimal] = None
    note: Optional[str] = Field(default=None, max_length=200)


class CloudAnchorVerify(BaseModel):
    """검증 결과 기록 — 앱 재시작 후 리졸브 성공/실패(11단계 §4①).

    ⚠️ 호스팅한 세션에서 그대로 리졸브한 결과를 여기로 보내면 안 된다.
    그 세션엔 앵커가 이미 로컬에 있어 성공이 부풀려진다.
    """

    ok: bool
    latency_ms: Optional[int] = Field(default=None, ge=0)
