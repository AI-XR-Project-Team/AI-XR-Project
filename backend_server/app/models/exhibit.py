from sqlalchemy import Column, DateTime, ForeignKey, String, text
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import relationship

from app.core.db import Base


class Exhibit(Base):
    __tablename__ = "exhibits"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    dinosaur_id = Column(UUID(as_uuid=True),
                         ForeignKey("dinosaurs.id", ondelete="CASCADE"),
                         nullable=False)
    label = Column(String(200), nullable=False)
    anchor_hint = Column(String(200))
    # 네비 그래프 노드 매핑 (도착 목적지 연결). 노드 삭제 시 SET NULL.
    nav_node_id = Column(UUID(as_uuid=True),
                         ForeignKey("nav_nodes.id", ondelete="SET NULL"))
    created_at = Column(DateTime(timezone=True), server_default=text("now()"))

    dinosaur = relationship("Dinosaur")
    # POI 목록 (pos_z_cm 오름차순). GET /exhibits/{id} 응답에 포함된다.
    pois = relationship("Poi", backref="exhibit",
                        order_by="Poi.pos_z_cm")
    nav_node = relationship("NavNode", uselist=False)
