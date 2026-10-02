from sqlalchemy import Column, ForeignKey, Numeric, String, text
from sqlalchemy.dialects.postgresql import UUID

from app.core.db import Base


class NavNode(Base):
    __tablename__ = "nav_nodes"

    id = Column(UUID(as_uuid=True), primary_key=True,
                server_default=text("gen_random_uuid()"))
    map_id = Column(UUID(as_uuid=True),
                    ForeignKey("map_spaces.id", ondelete="CASCADE"),
                    nullable=False)
    # 좌표: UE5 Z-up, cm
    pos_x_cm = Column(Numeric(8, 2), nullable=False)
    pos_y_cm = Column(Numeric(8, 2), nullable=False)
    pos_z_cm = Column(Numeric(8, 2), nullable=False)
    # junction|waypoint|exhibit|entrance|facility (UI 분류 태그, 라우팅 무관)
    node_type = Column(String(20), nullable=False,
                       server_default=text("'waypoint'"))
    label = Column(String(200))
