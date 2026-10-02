from sqlalchemy import Boolean, Column, ForeignKey, Numeric, text
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import relationship

from app.core.db import Base


class NavEdge(Base):
    __tablename__ = "nav_edges"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    map_id = Column(UUID(as_uuid=True),
                    ForeignKey("map_spaces.id", ondelete="CASCADE"),
                    nullable=False)
    from_node_id = Column(UUID(as_uuid=True),
                          ForeignKey("nav_nodes.id", ondelete="CASCADE"),
                          nullable=False)
    to_node_id = Column(UUID(as_uuid=True),
                        ForeignKey("nav_nodes.id", ondelete="CASCADE"),
                        nullable=False)
    distance_cm = Column(Numeric(9, 2))             # NULL=좌표로 자동 계산
    bidirectional = Column(Boolean, nullable=False,
                           server_default=text("true"))
    # false=계단 등, 휠체어 경로 제외용
    accessible = Column(Boolean, nullable=False, server_default=text("true"))

    from_node = relationship("NavNode", foreign_keys=[from_node_id])
    to_node = relationship("NavNode", foreign_keys=[to_node_id])
