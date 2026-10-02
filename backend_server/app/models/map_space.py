from sqlalchemy import Column, DateTime, String, Text, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class MapSpace(Base):
    __tablename__ = "map_spaces"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    name = Column(String(200), nullable=False)
    origin_note = Column(Text)                      # 원점·축 정의 서술
    coord_system = Column(String(50), nullable=False,
                          server_default=text("'ue5_zup_cm'"))
    # 벽 외곽선·내부 구조물(JSON 문자열: {"outline": [[x,y],...], "obstacles": [...]}).
    # 전체 지도(GET /maps/{id}/graph)가 그대로 꺼내 쓴다. 구버전 맵은 NULL(빈 배열로 취급).
    outline_json = Column(Text)
    created_at = Column(DateTime(timezone=True), server_default=text("now()"))
