from sqlalchemy import Column, ForeignKey, Numeric, String, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class Marker(Base):
    __tablename__ = "markers"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    map_id = Column(UUID(as_uuid=True),
                    ForeignKey("map_spaces.id", ondelete="CASCADE"),
                    nullable=False)
    code = Column(String(100), nullable=False, unique=True)  # QR 문자열 (조회 키)
    marker_type = Column(String(20), nullable=False,
                         server_default=text("'qr'"))    # qr (MVP) | image
    # 좌표: UE5 Z-up, cm
    pos_x_cm = Column(Numeric(8, 2), nullable=False)
    pos_y_cm = Column(Numeric(8, 2), nullable=False)
    pos_z_cm = Column(Numeric(8, 2), nullable=False)
    # 마커 정면 방향(+X 기준 CCW °)
    heading_deg = Column(Numeric(6, 2), nullable=False, server_default=text("0"))
    note = Column(String(200))
