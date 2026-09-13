from sqlalchemy import (Boolean, Column, DateTime, ForeignKey, Integer, Numeric,
                        String, UniqueConstraint, text)
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class CloudAnchor(Base):
    """ARCore Cloud Anchor ↔ 맵 좌표 매핑 + 수명(Management API 미러).

    markers 와 같은 측위 기준점이지만 물리 마커 대신 3D 특징점 앵커라 미관 훼손 0.
    DDL 진실 소스는 db/init/04_cloud_anchors.sql. 여기서는 매핑만 한다.
    """

    __tablename__ = "cloud_anchors"
    __table_args__ = (UniqueConstraint("map_id", "point_no", name="uq_cloud_anchors_point"),)

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    map_id = Column(UUID(as_uuid=True),
                    ForeignKey("map_spaces.id", ondelete="CASCADE"),
                    nullable=False)
    # 11단계 D5': 좌표(포인트)가 먼저 있고 cloud_id 가 나중에 바인딩된다.
    point_no = Column(Integer, nullable=False)                   # 지도 계획상 포인트 번호
    cloud_id = Column(String(200), unique=True)                  # 리졸브 키. 바인딩 전 NULL
    # 좌표: UE5 Z-up, cm
    pos_x_cm = Column(Numeric(8, 2), nullable=False)
    pos_y_cm = Column(Numeric(8, 2), nullable=False)
    pos_z_cm = Column(Numeric(8, 2), nullable=False)
    heading_deg = Column(Numeric(6, 2), nullable=False, server_default=text("0"))
    edge = Column(String(20))                                    # 놓인 네비 edge(예: "A-B")
    label = Column(String(200))
    enabled = Column(Boolean, nullable=False, server_default=text("true"))

    # Management API 미러 (extend_cloud_anchor_ttls.py 가 관리)
    create_time = Column(DateTime(timezone=True))
    expire_time = Column(DateTime(timezone=True))
    max_expire_time = Column(DateTime(timezone=True))
    last_localize_time = Column(DateTime(timezone=True))
    ttl_synced_at = Column(DateTime(timezone=True))
    ttl_extended_at = Column(DateTime(timezone=True))            # 365일 연장 성공 시각(NULL=미연장)
    # 11단계 §4① — 앱 재시작 후 리졸브 검증
    verified_at = Column(DateTime(timezone=True))
    verify_latency_ms = Column(Integer)
    note = Column(String(200))
